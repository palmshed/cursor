#include "services/ci_investigation_service.h"
#include "services/command_service.h"

#include <algorithm>
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

std::string CiInvestigationService::analyze_logs(int run_id) {
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
    return "Test assertion failure — check test expectations";
  if (text.find("not found") != std::string::npos ||
      text.find("No such") != std::string::npos)
    return "Missing file or dependency — check the referenced path";
  if (text.find("error:") != std::string::npos ||
      text.find("Error:") != std::string::npos)
    return "Compilation or runtime error — check the source file referenced in the error";
  if (text.find("timeout") != std::string::npos ||
      text.find("Timeout") != std::string::npos)
    return "Operation timed out — consider increasing timeout or optimizing the operation";
  if (text.find("Exit code 1") != std::string::npos)
    return "Command exited with non-zero status — check the failed command's output";
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

} // namespace Services
