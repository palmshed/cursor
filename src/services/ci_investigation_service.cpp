#include "services/ci_investigation_service.h"
#include "services/ai_service.h"
#include "services/command_service.h"
#include "core/model_catalog.h"
#include "utils/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace Services {

// ---------------------------------------------------------------------------
// Detect repo owner/name from git remote
// ---------------------------------------------------------------------------

std::string CiInvestigationService::detect_repo() {
  std::string remote = Services::CommandService::execute(
      "git remote get-url origin 2>/dev/null");
  if (remote.empty() || remote.find("Exit code:") != std::string::npos)
    return "";

  // Handle both HTTPS and SSH formats
  // https://github.com/owner/repo.git
  // git@github.com:owner/repo.git
  remote.erase(0, remote.find_first_not_of(" \t\n\r"));
  while (!remote.empty() &&
         (remote.back() == '\n' || remote.back() == '\r' ||
          remote.back() == ' ' || remote.back() == '.'))
    remote.pop_back();

  std::string owner_repo;
  size_t colon = remote.find(':');
  size_t slash = remote.find("github.com/");
  if (slash != std::string::npos) {
    owner_repo = remote.substr(slash + 11);  // after "github.com/"
  } else if (colon != std::string::npos &&
             remote.find("git@") != std::string::npos) {
    owner_repo = remote.substr(colon + 1);
  }

  // Strip trailing .git
  if (owner_repo.size() > 4 && owner_repo.substr(owner_repo.size() - 4) == ".git")
    owner_repo = owner_repo.substr(0, owner_repo.size() - 4);

  return owner_repo;
}

// ---------------------------------------------------------------------------
// List recent workflow runs
// ---------------------------------------------------------------------------

static std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\n\r");
  size_t end = s.find_last_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  return s.substr(start, end - start + 1);
}

std::vector<CiWorkflowRun> CiInvestigationService::list_recent_runs(int limit) {
  std::vector<CiWorkflowRun> runs;

  std::string output = Services::CommandService::execute(
      "gh run list --limit " + std::to_string(limit) +
      " --json databaseId,displayTitle,headBranch,conclusion,createdAt,workflowName 2>/dev/null");

  if (output.empty() || output.find("Exit code:") != std::string::npos ||
      output.find("not found") != std::string::npos)
    return runs;

  // Parse JSON array from gh CLI
  // Format: [{"databaseId":123,"displayTitle":"CI","headBranch":"main","conclusion":"success","createdAt":"...","workflowName":"CI"}]
  size_t pos = 0;
  while ((pos = output.find('{', pos)) != std::string::npos) {
    size_t end = output.find('}', pos);
    if (end == std::string::npos) break;

    std::string obj = output.substr(pos, end - pos + 1);
    pos = end + 1;

    CiWorkflowRun run;
    run.id = 0;

    auto extract = [&](const std::string &key) -> std::string {
      size_t k = obj.find("\"" + key + "\":");
      if (k == std::string::npos) return "";
      k = obj.find(':', k) + 1;
      while (k < obj.size() && (obj[k] == ' ' || obj[k] == '\t'))
        k++;
      if (obj[k] == '"') {
        k++;
        size_t q = obj.find('"', k);
        return (q == std::string::npos) ? "" : obj.substr(k, q - k);
      }
      // Number value
      size_t c = obj.find_first_of(",}", k);
      return (c == std::string::npos) ? "" : obj.substr(k, c - k);
    };

    std::string id_str = extract("databaseId");
    if (!id_str.empty()) {
      try {
        run.id = std::stoll(id_str);
      } catch (...) {
        run.id = 0;
      }
    }
    run.title = extract("displayTitle");
    run.branch = extract("headBranch");
    run.conclusion = extract("conclusion");
    run.created_at = extract("createdAt");
    run.workflow_name = extract("workflowName");

    if (!id_str.empty())
      runs.push_back(std::move(run));
  }

  return runs;
}

// ---------------------------------------------------------------------------
// Get failure details from failed runs
// ---------------------------------------------------------------------------

std::vector<CiFailureDetail> CiInvestigationService::get_failure_details(
    const std::vector<CiWorkflowRun> &runs) {

  std::vector<CiFailureDetail> failures;

  for (auto &run : runs) {
    if (run.conclusion != "failure")
      continue;

    CiFailureDetail fd;
    fd.run_id = run.id;

    // Get run view with failed jobs
    std::string view = Services::CommandService::execute(
        "gh run view " + std::to_string(run.id) +
        " --log --json 2>/dev/null | tail -100");
    // Note: --json was added recently. Fallback: --log and parse
    if (view.empty() || view.find("Exit code:") != std::string::npos) {
      view = Services::CommandService::execute(
          "gh run view " + std::to_string(run.id) +
          " --log 2>/dev/null | grep -E \"(FAIL|Error|error:|failed|Failure)\" | tail -30");
    }

    // Parse the log output for job/step names and error lines
    std::istringstream stream(view);
    std::string line;
    while (std::getline(stream, line)) {
      // Try to identify job/step from gh log format
      if (line.find("❌") != std::string::npos ||
          line.find("FAIL") != std::string::npos ||
          line.find("fail") != std::string::npos) {
        fd.step_name = trim(line);
      }

      // Collect error lines
      if (line.find("error:") != std::string::npos ||
          line.find("Error:") != std::string::npos ||
          line.find("FAILED") != std::string::npos ||
          line.find("Assertion") != std::string::npos ||
          line.find("Exit code 1") != std::string::npos) {
        fd.error_lines.push_back(trim(line));
      }
    }

    // Analyze error text for likely file
    if (!fd.error_lines.empty()) {
      fd.likely_file = extract_likely_file(fd.error_lines.front());
    }

    failures.push_back(std::move(fd));
  }

  return failures;
}

// ---------------------------------------------------------------------------
// Analyze full logs for a specific run
// ---------------------------------------------------------------------------

std::string CiInvestigationService::analyze_logs(long long run_id) {
  std::string logs = Services::CommandService::execute(
      "gh run view " + std::to_string(run_id) +
      " --log 2>/dev/null | grep -E \"(error|Error|ERROR|FAIL|failed|FAILED|Assertion|expected|not found|No such)\" | head -30");

  if (logs.empty() || logs.find("Exit code:") != std::string::npos)
    return "No log output available.";

  return logs;
}

// ---------------------------------------------------------------------------
// Suggest a fix based on error text
// ---------------------------------------------------------------------------

std::string CiInvestigationService::suggest_fix(const std::string &text) {
  if (text.find("Assertion") != std::string::npos ||
      text.find("expected") != std::string::npos)
    return "Test assertion failure -- check test expectations";
  if (text.find("not found") != std::string::npos ||
      text.find("No such") != std::string::npos)
    return "Missing file or dependency -- check the referenced path";
  if (text.find("error:") != std::string::npos ||
      text.find("Error:") != std::string::npos)
    return "Compilation or runtime error -- check the source file referenced in the error";
  if (text.find("timeout") != std::string::npos ||
      text.find("Timeout") != std::string::npos)
    return "Operation timed out -- consider increasing timeout or optimizing the operation";
  if (text.find("Exit code 1") != std::string::npos)
    return "Command exited with non-zero status -- check the failed command's output";
  return "Review the log output for details";
}

// ---------------------------------------------------------------------------
// Extract likely file path from an error message
// ---------------------------------------------------------------------------

std::string CiInvestigationService::extract_likely_file(const std::string &text) {
  // Look for file paths in the error (common patterns)
  std::vector<std::string> patterns = {
      "tests/", "src/", "include/", "lib/", "app/",
      "Makefile", "CMakeLists.txt", "package.json"
  };

  for (auto &p : patterns) {
    size_t pos = text.find(p);
    if (pos != std::string::npos) {
      // Extract from pattern start to next space/comma/quote
      size_t end = text.find_first_of(" ,\"':;)", pos);
      if (end == std::string::npos)
        return text.substr(pos);
      return text.substr(pos, end - pos);
    }
  }

  return "";
}

// ---------------------------------------------------------------------------
// Main investigation entry point
// ---------------------------------------------------------------------------

CiInvestigationResult CiInvestigationService::investigate() {
  CiInvestigationResult result;

  // 1. Detect repo
  result.repo = detect_repo();
  if (result.repo.empty()) {
    result.gh_available = false;
    result.summary = "No GitHub repository detected. Ensure you're in a git repo with a GitHub remote.";
    return result;
  }

  // 2. Check gh availability
  std::string gh_check = Services::CommandService::execute(
      "gh --version 2>/dev/null | head -1");
  if (gh_check.empty() || gh_check.find("not found") != std::string::npos) {
    result.gh_available = false;
    result.summary = "GitHub CLI (gh) is not installed. Install it with: brew install gh";
    return result;
  }

  // 3. Check gh auth
  std::string auth_check = Services::CommandService::execute(
      "gh auth status 2>&1 | head -3");
  if (auth_check.find("not logged in") != std::string::npos ||
      auth_check.find("Logged in") == std::string::npos) {
    result.gh_available = false;
    result.summary = "GitHub CLI is not authenticated. Run: gh auth login";
    return result;
  }

  result.gh_available = true;

  // 4. List recent runs
  result.recent_runs = list_recent_runs(5);

  // 5. Investigate failures
  result.failures = get_failure_details(result.recent_runs);

  // 6. Build summary
  std::string summary;
  int total = static_cast<int>(result.recent_runs.size());
  int failed = static_cast<int>(result.failures.size());
  int passed = 0;
  for (auto &r : result.recent_runs) {
    if (r.conclusion == "success") passed++;
  }

  summary += "Repo: " + result.repo + "\n";
  summary += "Recent runs: " + std::to_string(total)
          + " (" + std::to_string(passed) + " passed, "
          + std::to_string(failed) + " failed)\n";

  if (!result.failures.empty()) {
    summary += "\nFailure analysis:\n";
    for (auto &f : result.failures) {
      summary += "  Run #" + std::to_string(f.run_id) + "\n";
      if (!f.job_name.empty())
        summary += "    Job: " + f.job_name + "\n";
      if (!f.step_name.empty())
        summary += "    Step: " + f.step_name + "\n";
      for (auto &err : f.error_lines) {
        summary += "    " + err + "\n";
      }
      if (!f.likely_file.empty())
        summary += "    Likely file: " + f.likely_file + "\n";
    }
  }

  result.summary = summary;
  return result;
}

// Forward declaration for JSON helper used by get_failed_steps
static std::string extract_val_in(const std::string &obj, const std::string &key);

// ---------------------------------------------------------------------------
// Step 1: Deterministic failed-run targeting
// ---------------------------------------------------------------------------

long long CiInvestigationService::get_latest_failure_run_id() {
  std::string output = Services::CommandService::execute(
      "gh run list --limit 10 "
      "--json databaseId,conclusion 2>/dev/null");

  if (output.empty() || output.find("Exit code:") != std::string::npos)
    return 0;

  // Parse JSON array to find first failure
  size_t pos = 0;
  while ((pos = output.find('{', pos)) != std::string::npos) {
    size_t end = output.find('}', pos);
    if (end == std::string::npos) break;
    std::string obj = output.substr(pos, end - pos + 1);
    pos = end + 1;

    auto extract_val = [&](const std::string &key) -> std::string {
      size_t k = obj.find("\"" + key + "\":");
      if (k == std::string::npos) return "";
      k = obj.find(':', k) + 1;
      while (k < obj.size() && (obj[k] == ' ' || obj[k] == '\t')) k++;
      if (obj[k] == '"') {
        k++;
        size_t q = obj.find('"', k);
        return (q == std::string::npos) ? "" : obj.substr(k, q - k);
      }
      size_t c = obj.find_first_of(",}", k);
      return (c == std::string::npos) ? "" : obj.substr(k, c - k);
    };

    std::string conclusion = extract_val("conclusion");
    if (conclusion == "failure") {
      std::string id_str = extract_val("databaseId");
      if (!id_str.empty()) {
        try { return std::stoll(id_str); } catch (...) { return 0; }
      }
    }
  }

  return 0;
}

// ---------------------------------------------------------------------------
// Step 2: Deterministic failed-step extraction via gh run view --json jobs
// ---------------------------------------------------------------------------

std::vector<CiFailureDetail> CiInvestigationService::get_failed_steps(
    long long run_id) {

  std::string output = Services::CommandService::execute(
      "gh run view " + std::to_string(run_id) +
      " --json jobs 2>/dev/null");

  if (output.empty() || output.find("Exit code:") != std::string::npos)
    return {};

  return parse_failed_steps_json(output, run_id);
}

std::vector<CiFailureDetail> CiInvestigationService::parse_failed_steps_json(
    const std::string &json_output, long long run_id) {

  std::vector<CiFailureDetail> failures;

  // Output: {"jobs":[{"name":"...","conclusion":"...","steps":[...]}]}
  size_t jobs_start = json_output.find("\"jobs\"");
  if (jobs_start == std::string::npos) return failures;
  jobs_start = json_output.find(':', jobs_start);
  if (jobs_start == std::string::npos) return failures;
  jobs_start = json_output.find('[', jobs_start);
  if (jobs_start == std::string::npos) return failures;
  jobs_start++; // past '['

  size_t brace_depth = 0;
  size_t job_start = jobs_start;

  for (size_t i = jobs_start; i < json_output.size(); i++) {
    if (json_output[i] == '{') {
      if (brace_depth == 0) job_start = i;
      brace_depth++;
    }
    if (json_output[i] == '}') {
      brace_depth--;
      if (brace_depth == 0) {
        std::string job_obj = json_output.substr(job_start, i - job_start + 1);

        auto extract_val = [&](const std::string &key) -> std::string {
          size_t k = job_obj.find("\"" + key + "\":");
          if (k == std::string::npos) return "";
          k = job_obj.find(':', k) + 1;
          while (k < job_obj.size() && (job_obj[k] == ' ' || job_obj[k] == '\t')) k++;
          if (job_obj[k] == '"') {
            k++;
            size_t q = job_obj.find('"', k);
            return (q == std::string::npos) ? "" : job_obj.substr(k, q - k);
          }
          size_t c = job_obj.find_first_of(",}", k);
          return (c == std::string::npos) ? "" : job_obj.substr(k, c - k);
        };

        std::string job_conclusion = extract_val("conclusion");
        std::string job_name = extract_val("name");

        if (job_conclusion == "failure" && !job_name.empty()) {
          bool found_steps = false;
          size_t steps_start = job_obj.find("\"steps\"");
          if (steps_start != std::string::npos) {
            steps_start = job_obj.find(':', steps_start);
            if (steps_start != std::string::npos) {
              steps_start = job_obj.find('[', steps_start);
              if (steps_start != std::string::npos) {
                found_steps = true;
                steps_start++; // past '['
                size_t step_brace = 0;
                size_t step_start = steps_start;
                for (size_t j = steps_start; j < job_obj.size(); j++) {
                  if (job_obj[j] == '{') {
                    if (step_brace == 0) step_start = j;
                    step_brace++;
                  }
                  if (job_obj[j] == '}') {
                    step_brace--;
                    if (step_brace == 0) {
                      std::string step_obj = job_obj.substr(step_start, j - step_start + 1);
                      std::string step_name = extract_val_in(step_obj, "name");
                      std::string step_conclusion = extract_val_in(step_obj, "conclusion");

                      if (step_conclusion == "failure" && !step_name.empty()) {
                        CiFailureDetail fd;
                        fd.run_id = run_id;
                        fd.job_name = job_name;
                        fd.step_name = step_name;
                        failures.push_back(std::move(fd));
                      }
                    }
                  }
                  if (step_brace == 0 && job_obj[j] == ']') break;
                }
              }
            }
          }
          if (!found_steps) {
            CiFailureDetail fd;
            fd.run_id = run_id;
            fd.job_name = job_name;
            failures.push_back(std::move(fd));
          }
        }
      }
    }
    if (brace_depth == 0 && json_output[i] == ']') break;
  }

  return failures;
}

// Helper: extract a JSON string value from a substring
static std::string extract_val_in(const std::string &obj, const std::string &key) {
  size_t k = obj.find("\"" + key + "\":");
  if (k == std::string::npos) return "";
  k = obj.find(':', k) + 1;
  while (k < obj.size() && (obj[k] == ' ' || obj[k] == '\t')) k++;
  if (k >= obj.size()) return "";
  if (obj[k] == '"') {
    k++;
    size_t q = obj.find('"', k);
    return (q == std::string::npos) ? "" : obj.substr(k, q - k);
  }
  size_t c = obj.find_first_of(",}", k);
  return (c == std::string::npos) ? "" : obj.substr(k, c - k);
}

// ---------------------------------------------------------------------------
// Step 3: Deterministic error-snippet extraction
// ---------------------------------------------------------------------------

std::string CiInvestigationService::get_error_snippet(long long run_id) {
  std::string logs = Services::CommandService::execute(
      "gh run view " + std::to_string(run_id) +
      " --log 2>/dev/null | grep -E \"(error|Error|ERROR|FAIL|failed|FAILED|"
      "Assertion|expected|not found|No such|Exit code|fatal|"
      "undefined reference|No matching|warning:)\" | head -30");

  if (logs.empty() || logs.find("Exit code:") != std::string::npos)
    return "";

  // Collect up to 15 unique error lines
  std::vector<std::string> lines;
  std::istringstream stream(logs);
  std::string line;
  while (std::getline(stream, line)) {
    // Trim leading timestamp/log level noise
    size_t content_pos = line.find("build (");
    if (content_pos == std::string::npos)
      content_pos = line.find("e2e");
    if (content_pos == std::string::npos)
      content_pos = line.find('\t');
    if (content_pos == std::string::npos)
      content_pos = 0;
    else
      content_pos = line.find('\t', content_pos);
    if (content_pos == std::string::npos || content_pos + 1 >= line.size())
      content_pos = 0;
    else
      content_pos++;

    std::string content = line.substr(content_pos);
    if (content.empty()) continue;

    // Skip lines with only whitespace/formatting
    bool meaningful = false;
    for (auto c : content) {
      if (isprint(static_cast<unsigned char>(c)) && !isspace(static_cast<unsigned char>(c))) {
        meaningful = true;
        break;
      }
    }
    if (!meaningful) continue;

    lines.push_back(content);
    if (lines.size() >= 15) break;
  }

  // If no error lines found via grep, try raw log tail
  if (lines.empty()) {
    std::string raw = Services::CommandService::execute(
        "gh run view " + std::to_string(run_id) +
        " --log 2>/dev/null | tail -50");
    std::istringstream raw_stream(raw);
    std::string raw_line;
    while (std::getline(raw_stream, raw_line)) {
      std::string trimmed = trim(raw_line);
      if (!trimmed.empty()) {
        lines.push_back(trimmed);
        if (lines.size() >= 10) break;
      }
    }
  }

  std::string result;
  for (auto &l : lines) {
    result += l + "\n";
  }
  return result;
}

// ---------------------------------------------------------------------------
// Combined: steps 2+3 for a single run ID
// ---------------------------------------------------------------------------

std::string CiInvestigationService::extract_ci_failure(long long run_id) {
  // Step 2: get failed steps
  auto steps = get_failed_steps(run_id);

  // Build the failure report
  std::string report;

  if (steps.empty()) {
    report += "Run #" + std::to_string(run_id) + ": no failed steps found.\n";
    // Try to get run info
    std::string info = Services::CommandService::execute(
        "gh run view " + std::to_string(run_id) +
        " --json conclusion,displayTitle,workflowName 2>/dev/null");
    if (!info.empty() && info.find("Exit code:") == std::string::npos) {
      report += "Run info: " + info.substr(0, 200) + "\n";
    }
    return report;
  }

  report += "Workflow: " + steps[0].job_name + "\n";
  for (auto &f : steps) {
    if (!f.job_name.empty())
      report += "Job: " + f.job_name + "\n";
    if (!f.step_name.empty())
      report += "Step: " + f.step_name + "\n";
    report += "Conclusion: failure\n";
  }

  // Step 3: get error snippet
  std::string snippet = get_error_snippet(run_id);
  if (!snippet.empty()) {
    report += "Error:\n";
    report += snippet;
  }

  return report;
}

// ---------------------------------------------------------------------------
// Step 5: Root-cause synthesis (AI layer)
// ---------------------------------------------------------------------------

// Try to detect an available AI provider from the environment.
// Returns nullptr if none found.
static const Core::ModelConfig* detect_ai_provider(std::string &out_api_key) {
  // Ordered preference: first key found wins.
  static const std::pair<const char*, Core::Provider> kProviders[] = {
    {"TOGETHER_API_KEY",  Core::Provider::Together},
    {"CEREBRAS_API_KEY",  Core::Provider::Cerebras},
    {"FIREWORKS_API_KEY", Core::Provider::Fireworks},
    {"GROQ_API_KEY",      Core::Provider::Groq},
    {"DEEPSEEK_API_KEY",  Core::Provider::DeepSeek},
    {"OPENAI_API_KEY",    Core::Provider::OpenAI},
  };
  for (auto& [env_var, provider] : kProviders) {
    std::string key = Utils::Config::get_env_var(env_var);
    if (!key.empty()) {
      out_api_key = key;
      // Return the first catalog model for this provider.
      auto models = Core::ModelCatalog::models_for_provider(provider);
      if (!models.empty()) return models.front();
    }
  }
  // Check for local Ollama.
  if (!Utils::Config::get_env_var("OLLAMA_HOST").empty() ||
      Utils::Config::has_env_var("OLLAMA_HOST")) {
    auto models = Core::ModelCatalog::models_for_provider(Core::Provider::Ollama);
    if (!models.empty()) return models.front();
  }
  return nullptr;
}

std::string CiInvestigationService::synthesize_root_cause(long long run_id) {
  // Build structured failure context (job/step only, no pre-filtered error snippet)
  auto steps = get_failed_steps(run_id);
  std::string context;

  if (steps.empty()) {
    context = "Run #" + std::to_string(run_id) + ": no failed steps found.\n";
  } else {
    context += "Workflow run #" + std::to_string(run_id) + "\n";
    for (auto &f : steps) {
      if (!f.job_name.empty())
        context += "  Job: " + f.job_name + "\n";
      if (!f.step_name.empty())
        context += "  Step: " + f.step_name + "\n";
    }
  }

  // Try to detect AI provider
  std::string api_key;
  const Core::ModelConfig* model = detect_ai_provider(api_key);
  if (!model) {
    // Fall back to deterministic report with snippet
    return extract_ci_failure(run_id) +
        "\n[AI analysis unavailable: no AI provider configured]\n";
  }

  try {
    Services::AIService ai(*model, api_key);
    if (!ai.is_available()) {
      return extract_ci_failure(run_id) +
          "\n[AI analysis unavailable: provider not ready]\n";
    }

    std::string prompt =
        "You are a CI failure analyst. Review the following GitHub Actions "
        "workflow run and identify the root cause.\n\n"
        "Instructions:\n"
        "- Examine the failed jobs and steps below\n"
        "- Determine the most likely root cause\n"
        "- Suggest a fix\n\n"
        + context +
        "\nRoot cause analysis:\n";

    std::string analysis = ai.chat(prompt, "", "You are a CI failure analyst. Review the GitHub Actions workflow run and identify the root cause.");
    return context + "\n--- AI Root-Cause Analysis ---\n" + analysis + "\n";
  } catch (std::exception &e) {
    return extract_ci_failure(run_id) +
        "\n[AI analysis error: " + e.what() + "]\n";
  }
}

} // namespace Services
