#include "services/execution_engine.h"
#include "services/ai_service.h"
#include "services/confidence_service.h"
#include "ui/ui_manager.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>
#include <unordered_set>

namespace Services {

namespace {
// Helper for tokenizing a string while preserving symbol characters (alnum, _, :, -, ., /)
[[maybe_unused]] std::vector<std::string> split_into_words(const std::string &str) {
  std::vector<std::string> words;
  std::string current;
  for (char c : str) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '-' || c == '.' || c == '/') {
      current.push_back(c);
    } else {
      if (!current.empty()) {
        words.push_back(current);
        current.clear();
      }
    }
  }
  if (!current.empty()) {
    words.push_back(current);
  }
  return words;
}

[[maybe_unused]] bool is_stop_word(const std::string &word) {
  std::string lower = word;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  static const std::unordered_set<std::string> stop_words = {
    "how", "what", "where", "why", "tell", "about", "show", "find",
    "is", "are", "was", "were", "do", "does", "did", "we", "you", "me",
    "the", "a", "an", "this", "that", "in", "of", "to", "for", "and", "or",
    "call", "called", "use", "used", "implement", "implemented",
    "define", "defined", "reference", "referenced", "explain", "locate",
    "search", "grep", "works", "work", "with", "by", "from", "at", "on",
    "here", "there", "who", "whom", "which", "my", "our", "your", "their",
    "his", "her", "its", "can", "could", "should", "would", "will", "shall",
    "please", "give", "get", "got", "make", "made", "go", "gone", "went",
    "struct", "class", "function", "method", "variable", "file", "files",
    "header", "implementation", "definition", "service", "manager", "code",
    "project", "codebase", "repo", "repository", "declaration", "enum",
    "utility", "heuristic", "results", "target", "usage", "responsibilities",
    "happen", "executable", "system", "detail", "analysis", "structs", "classes",
    "functions", "methods", "variables", "headers", "implementations",
    "definitions", "services", "managers"
  };
  return stop_words.count(lower) > 0;
}
} // namespace

// ---------------------------------------------------------------------------
// EvidenceStore
// ---------------------------------------------------------------------------

void EvidenceStore::add_fact(const std::string &fact) {
  facts.push_back(fact);
}

bool EvidenceStore::has_fact_containing(const std::string &keyword) const {
  for (auto &f : facts) {
    if (f.find(keyword) != std::string::npos)
      return true;
  }
  return false;
}

void EvidenceStore::mark_evidence_class(EvidenceClass ec) {
  for (auto &c : classes)
    if (c == ec)
      return;
  classes.push_back(ec);
}

bool EvidenceStore::has_any_evidence_class(const std::vector<EvidenceClass> &required) const {
  for (auto &req : required)
    for (auto &c : classes)
      if (c == req)
        return true;
  return false;
}

bool EvidenceStore::has_all_evidence_classes(const std::vector<EvidenceClass> &required) const {
  for (auto &req : required) {
    bool found = false;
    for (auto &c : classes)
      if (c == req) { found = true; break; }
    if (!found)
      return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Goal classification
// ---------------------------------------------------------------------------

static bool word_boundary(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\0' ||
         c == '.' || c == ',' || c == '!' || c == '?' ||
         c == '/' || c == ':' ||
         c == ')' || c == ']' || c == '}';
}

static bool contains_any(const std::string &text,
                         const std::vector<std::string> &terms) {
  std::string lower = text;
  for (auto &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (auto &t : terms) {
    std::string tl = t;
    for (auto &c : tl)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t pos = 0;
    while ((pos = lower.find(tl, pos)) != std::string::npos) {
      // For multi-word terms, ensure word-boundary before and after
      bool boundary_before = (pos == 0) || word_boundary(lower[pos - 1]);
      bool boundary_after = (pos + tl.size() >= lower.size()) ||
                             word_boundary(lower[pos + tl.size()]);
      if (boundary_before && boundary_after)
        return true;
      pos++;
    }
  }
  return false;
}

ExecutionEngine::GoalType ExecutionEngine::classify_goal(
    const std::string &goal) {
  // Call-site, usage, definition queries strongly suggest codebase query even if they mention CI/external command keywords
  if (contains_any(goal, {"call", "called", "used", "using", "implement", "implemented",
                          "define", "defined", "reference", "referenced",
                          "where is", "where do we", "where are"})) {
    if (contains_any(goal, {"tell me about", "overview", "describe", "what is this"}) &&
        contains_any(goal, {"codebase", "project", "repo", "repository", "application"})) {
      return CodebaseOverview;
    }
    return CodebaseQuery;
  }

  // Architecture / conceptual questions suggesting CodebaseOverview
  if (contains_any(goal, {"architecture", "design", "how it works", "how does it work"}) ||
      (contains_any(goal, {"how", "explain", "tell me"}) && contains_any(goal, {"work", "works"}))) {
    return CodebaseOverview;
  }

  if (mode_ == ClassifierMode::LLM && ai_)
    return classify_goal_llm(goal);

  // Check for GitHub Actions URLs before general codebase patterns
  if (contains_any(goal, {"github.com", "actions/runs", "github action",
                           "workflow run", "ci run"}))
    return GitHubInvestigation;

  if (contains_any(goal, {"ci", "github action", "workflow", "gh run",
                           "ci/cd", "actions"}))
    return CICheck;

  // Exclude general chat patterns before checking codebase keywords
  if (contains_any(goal, {"how are you", "how do you", "how do i",
                           "how does one", "how can i", "how can you",
                           "what is the difference", "what is a",
                           "who are you", "what can you"}))
    return GeneralChat;

  // Codebase overview: broad questions about the entire project
  if (contains_any(goal, {"tell me about", "overview", "describe",
                            "what is this"}) &&
      contains_any(goal, {"codebase", "project", "repo", "repository",
                           "application"}) &&
      detect_evidence_need(goal) != EvidenceNeed::CommitHistory)
    return CodebaseOverview;

  if (contains_any(goal, {"where", "what is", "what does", "what's",
                           "how does", "how is", "how are", "how", "works",
                           "find", "search", "grep", "locate",
                           "show me", "list", "tell me about", "tell me how",
                           "explain", "describe", "overview",
                           "architecture",
                           "in this project", "in this repo"}))
    return CodebaseQuery;

  if (contains_any(goal, {"add", "implement", "refactor", "fix", "migrate",
                           "create", "remove", "update", "upgrade",
                           "delete", "rename", "extract", "build",
                           "setup", "configure"}))
    return CodeChange;

  return GeneralChat;
}

ExecutionEngine::GoalType ExecutionEngine::classify_goal_llm(
    const std::string &goal) {
  std::string prompt =
    "Classify the following request into exactly one category. "
    "Respond with ONLY the category name, nothing else.\n\n"
    "Categories:\n"
    "GeneralChat - Greetings, how-to questions, general conversation\n"
    "CodebaseOverview - High-level questions about the entire project or codebase\n"
    "CodebaseQuery - Questions about specific code, looking up definitions, searching\n"
    "CodeChange - Add, modify, or remove code\n"
    "CICheck - CI/CD pipeline status or investigation\n"
    "GitHubInvestigation - GitHub Actions run investigation\n\n"
    "Request: " + goal + "\n\nCategory:";

  std::string response = ai_->chat(prompt, "");
  for (auto &c : response) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (response.find("codebaseoverview") != std::string::npos) return CodebaseOverview;
  if (response.find("codebasequery") != std::string::npos) return CodebaseQuery;
  if (response.find("codechange") != std::string::npos) return CodeChange;
  if (response.find("cicheck") != std::string::npos) return CICheck;
  if (response.find("githubinvestigation") != std::string::npos) return GitHubInvestigation;
  return GeneralChat;
}

std::string ExecutionEngine::goal_type_name(GoalType t) {
  switch (t) {
    case CICheck: return "CI Investigation";
    case CodebaseQuery: return "Repository Investigation";
    case CodebaseOverview: return "Codebase Overview";
    case CodeChange: return "Code Change";
    case GitHubInvestigation: return "GitHub Investigation";
    case GeneralChat: return "General Chat";
  }
  return "Unknown";
}

EvidenceNeed ExecutionEngine::detect_evidence_need(
    const std::string &goal) {
  if (contains_any(goal, {"commit", "history",
                           "change log", "what changed",
                           "previous version", "recent change"}))
    return EvidenceNeed::CommitHistory;
  return EvidenceNeed::Default;
}

std::vector<EvidenceClass> ExecutionEngine::required_evidence(
    const std::string &goal, GoalType type) {
  switch (type) {
    case CICheck:
      return {EvidenceClass::CIWorkflow};
    case GitHubInvestigation:
      return {EvidenceClass::CIWorkflow};
    case CodebaseQuery:
      switch (detect_evidence_need(goal)) {
        case EvidenceNeed::CommitHistory:
          return {EvidenceClass::GitLog};
        default:
          return {EvidenceClass::FileSearch, EvidenceClass::FileContent};
      }
    case CodebaseOverview:
      return {EvidenceClass::Discovery, EvidenceClass::FileContent};
    case CodeChange:
      return {EvidenceClass::Discovery,
              EvidenceClass::FileSearch,
              EvidenceClass::FileContent,
              EvidenceClass::Build,
              EvidenceClass::Test};
    case GeneralChat:
      return {};
  }
  return {};
}

// ---------------------------------------------------------------------------
// Tool selection
// ---------------------------------------------------------------------------

ToolCall ExecutionEngine::select_next_tool(
    const std::string &goal, GoalType type, const EvidenceStore &evidence) {
  if (mode_ == ClassifierMode::LLM && ai_)
    return select_next_tool_llm(goal, type, evidence);

  switch (type) {
    case CICheck: {
      // 1. List recent runs
      if (!evidence.has_fact_containing("gh run list"))
        return {"gh", "run list --limit 5 --json databaseId,displayTitle,headBranch,conclusion,createdAt,workflowName"};

      // 2. Read workflow files if CI failures found
      if (!evidence.has_fact_containing("read workflow") &&
          evidence.has_fact_containing("failure")) {
        // Find the workflow file from evidence
        for (auto &f : evidence.facts) {
          if (f.find(".yml") != std::string::npos ||
              f.find(".yaml") != std::string::npos) {
            return {"grep", ".github/workflows/"};
          }
        }
        return {"read", ".github/workflows/"};
      }

      return {};
    }

    case GitHubInvestigation: {
      // Extract run ID from GitHub Actions URL in the goal
      std::regex url_regex(R"(github\.com/[^/]+/[^/]+/actions/runs/(\d+))");
      std::smatch match;
      std::string run_id;
      if (std::regex_search(goal, match, url_regex) && match.size() > 1) {
        run_id = match[1].str();
      }
      if (run_id.empty()) return {}; // No run ID found, can't investigate

      // 1. Fetch run details
      if (!evidence.has_fact_containing("gh run view")) {
        return {"gh", "run view " + run_id +
                " --json conclusion,displayTitle,headBranch,createdAt,workflowName,jobs"};
      }

      // 2. Fetch job logs
      if (!evidence.has_fact_containing("--log")) {
        return {"gh", "run view " + run_id + " --log"};
      }

      return {};
    }

    case CodebaseOverview: {
      // 1. Discover project structure
      if (!evidence.has_fact_containing("discovery"))
        return {"discovery", ""};
      // 2. Read key project files
      if (!evidence.has_fact_containing("read"))
        return {"read", "README.md CMakeLists.txt AGENTS.md"};
      return {};
    }

    case CodebaseQuery: {
      auto need = detect_evidence_need(goal);
      if (need == EvidenceNeed::CommitHistory) {
        if (!evidence.has_fact_containing("git log"))
          return {"git", "log --oneline -10"};
        if (!evidence.has_fact_containing("git show"))
          return {"git", "log -1 --format=\"%H %s%n%b\""};
        return {};
      }
      // 1. Search the codebase
      if (!evidence.has_fact_containing("grep") &&
          !evidence.has_fact_containing("search")) {
        // Extract search term from goal (case-insensitive prefix matching)
        std::string term = goal;
        
        // Extract quoted terms if present
        size_t first_quote = term.find('"');
        size_t last_quote = term.rfind('"');
        if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
          term = term.substr(first_quote + 1, last_quote - first_quote - 1);
          return {"grep", term};
        } else {
          first_quote = term.find('\'');
          last_quote = term.rfind('\'');
          if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
            term = term.substr(first_quote + 1, last_quote - first_quote - 1);
            return {"grep", term};
          }
        }

        std::string lower_term = term;
        for (auto &c : lower_term) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        
        // Remove common prefixes (longest first to handle compounds)
        for (auto &prefix : {"tell me about ", "find where ", "search for ",
                             "where is ", "what is ", "what does ",
                             "how does ", "how is ", "how do we ", "where do we ", "show me ", "locate ",
                             "find ", "where ", "grep ", "explain ", "tell me how "}) {
          size_t p = lower_term.find(prefix);
          if (p == 0) {
            term = term.substr(p + strlen(prefix));
            lower_term = lower_term.substr(p + strlen(prefix));
            break;
          }
        }
        
        // Remove common suffixes
        for (const char *suffix : {" in this project", " in this codebase",
                             " in this repo", " in the code",
                             " in code", " in files", " is defined",
                             " is implemented", " used", " called", " implemented"}) {
          size_t p = lower_term.rfind(suffix);
          if (p != std::string::npos && p + strlen(suffix) == lower_term.size()) {
            term = term.substr(0, p);
            lower_term = lower_term.substr(0, p);
            break;
          }
        }

        // Clean punctuation
        while (!term.empty() && std::ispunct(static_cast<unsigned char>(term.back())))
          term.pop_back();

        // Tokenize and prefer noun phrases/multi-word terms
        std::vector<std::string> words = split_into_words(term);
        std::vector<std::vector<std::string>> phrase_groups;
        std::vector<std::string> current_group;

        for (const auto &w : words) {
          if (is_stop_word(w)) {
            if (!current_group.empty()) {
              phrase_groups.push_back(current_group);
              current_group.clear();
            }
          } else {
            current_group.push_back(w);
          }
        }
        if (!current_group.empty()) {
          phrase_groups.push_back(current_group);
        }

        // Select the best phrase group
        // First, look for multi-word terms (size >= 2)
        std::vector<std::string> best_multi_word;
        for (const auto &group : phrase_groups) {
          if (group.size() >= 2) {
            if (best_multi_word.empty() || group.size() > best_multi_word.size()) {
              best_multi_word = group;
            }
          }
        }

        if (!best_multi_word.empty()) {
          std::string reconstructed;
          for (size_t i = 0; i < best_multi_word.size(); ++i) {
            if (i > 0) reconstructed += "[ _-]?";
            reconstructed += best_multi_word[i];
          }
          term = reconstructed;
        } else if (!phrase_groups.empty()) {
          // If no multi-word term, take the first single content word
          std::string first_content;
          for (const auto &group : phrase_groups) {
            if (!group.empty()) {
              first_content = group[0];
              break;
            }
          }
          if (!first_content.empty()) {
            term = first_content;
          }
        }

        return {"grep", term};
      }

       // 2. Read files found by grep
      if (evidence.has_fact_containing("grep") &&
          !evidence.has_fact_containing("read")) {
        return {"read", ""};
      }

      return {};
    }

    case CodeChange: {
      // 1. Discovery
      if (!evidence.has_fact_containing("discovery"))
        return {"discovery", ""};

      // 2. Grep relevant files
      if (!evidence.has_fact_containing("grep"))
        return {"grep", ""};

      // 3. Read key files
      if (!evidence.has_fact_containing("read"))
        return {"read", ""};

      // 4. Build
      if (!evidence.has_fact_containing("build"))
        return {"cmake", "--build build"};

      // 5. Tests
      if (!evidence.has_fact_containing("test"))
        return {"ctest", "--test-dir build"};

      return {};
    }

    case GeneralChat:
      return {};
  }

  return {};
}

ToolCall ExecutionEngine::select_next_tool_llm(
    const std::string &goal, GoalType type, const EvidenceStore &evidence) {
  std::string prompt = "You are investigating a codebase.\n";
  prompt += "Goal type: " + goal_type_name(type) + "\n";
  prompt += "User request: " + goal + "\n\n";

  // Gate awareness: tell the LLM what evidence classes are still needed
  auto required = required_evidence(goal, type);
  if (!required.empty()) {
    prompt += "Required evidence (ALL must be satisfied to complete):\n";
    for (auto &ec : required) {
      bool have_it = false;
      for (auto &c : evidence.classes)
        if (c == ec) { have_it = true; break; }
      const char *name = "Unknown";
      switch (ec) {
        case EvidenceClass::FileSearch: name = "FileSearch (use grep)"; break;
        case EvidenceClass::FileContent: name = "FileContent (use read)"; break;
        case EvidenceClass::Discovery: name = "Discovery (use discovery)"; break;
        case EvidenceClass::GitLog: name = "GitLog (use git)"; break;
        case EvidenceClass::Build: name = "Build (use cmake)"; break;
        case EvidenceClass::Test: name = "Test (use ctest)"; break;
        case EvidenceClass::CIWorkflow: name = "CIWorkflow (use gh)"; break;
      }
      prompt += std::string("  ") + name + (have_it ? " [DONE]" : " [NEEDED]") + "\n";
    }
    prompt += "\nPrioritize tools that satisfy [NEEDED] evidence classes.\n\n";
  }

  prompt += "Evidence collected so far:\n";
  for (auto &f : evidence.facts)
    prompt += "  " + f + "\n";
  prompt +=
    "\nChoose the next tool. Options:\n"
    "  grep <query> - Search codebase\n"
    "  read - Read files from grep results\n"
    "  git <args> - Git command\n"
    "  discovery - Project structure\n"
    "  gh <args> - GitHub CLI\n"
    "  cmake <args> - Build\n"
    "  ctest <args> - Test\n"
    "  done - No more tools needed\n\n"
    "Respond with exactly one option, no explanation:";

  std::string response = ai_->chat(prompt, "");
  for (auto &c : response) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  size_t p;
  if ((p = response.find("grep ")) != std::string::npos)
    return {"grep", response.substr(p + 5)};
  if (response == "read" || response.starts_with("read "))
    return {"read", response.starts_with("read ") ? response.substr(5) : ""};
  if (response.find("git ") != std::string::npos)
    return {"git", response.substr(response.find("git ") + 4)};
  if (response.find("discovery") != std::string::npos)
    return {"discovery", ""};
  if ((p = response.find("gh ")) != std::string::npos)
    return {"gh", response.substr(p + 3)};
  if ((p = response.find("cmake")) != std::string::npos)
    return {"cmake", response.substr(p + 6)};
  if ((p = response.find("ctest")) != std::string::npos)
    return {"ctest", response.substr(p + 6)};

  return {};
}

// ---------------------------------------------------------------------------
// Completion check
// ---------------------------------------------------------------------------

bool ExecutionEngine::check_completion(const std::string &goal,
                                        GoalType type,
                                        const EvidenceStore &evidence) {
  // Evidence class gate: all required evidence classes must be present
  auto required = required_evidence(goal, type);
  if (!required.empty() &&
      !evidence.has_all_evidence_classes(required)) {
    return false;
  }

  switch (type) {
    case CICheck:
      return evidence.has_fact_containing("gh run list") &&
             (!evidence.has_fact_containing("failure") ||
              evidence.has_fact_containing("read workflow"));

    case GitHubInvestigation:
      return evidence.has_fact_containing("gh run view") &&
             evidence.has_fact_containing("--log");

    case CodebaseOverview:
      return evidence.has_fact_containing("discovery") &&
             evidence.has_fact_containing("read:results");

    case CodebaseQuery: {
      auto need = detect_evidence_need(goal);
      if (need == EvidenceNeed::CommitHistory)
        return evidence.has_fact_containing("git:results");
      return evidence.has_fact_containing("grep:results") &&
             evidence.has_fact_containing("read:results");
    }

    case CodeChange:
      return evidence.has_fact_containing("discovery") &&
             evidence.has_fact_containing("grep:results") &&
             evidence.has_fact_containing("read:results") &&
             evidence.has_fact_containing("build") &&
             evidence.has_fact_containing("test");

    case GeneralChat:
      return true;
  }
  return true;
}

bool ExecutionEngine::goal_is_achieved(const std::string &goal,
                                        const EvidenceStore &evidence) {
  return check_completion(goal, classify_goal(goal), evidence);
}

// ---------------------------------------------------------------------------
// Main execution loop
// ---------------------------------------------------------------------------

ExecutionResult ExecutionEngine::execute(const std::string &goal,
                                           ToolRunner run_tool,
                                           Core::UIManager &ui) {
  ExecutionResult result;
  EvidenceStore evidence;
  GoalType type = classify_goal(goal);

  ui.show_pipeline_section(goal_type_name(type));

  const int MAX_ITERATIONS = 20;
  std::vector<ConfidenceResult> confidence_history;
  int iteration_count = 0;

  for (iteration_count = 0; iteration_count < MAX_ITERATIONS; iteration_count++) {
    if (check_completion(goal, type, evidence))
      break;

    ToolCall tc = select_next_tool(goal, type, evidence);
    if (tc.tool.empty())
      break;

    ui.show_tool_invocation(tc.tool, tc.args);

    std::string output = run_tool(tc);
    ui.show_tool_output(output);

    std::string fact = tc.tool;
    if (!tc.args.empty())
      fact += " " + tc.args;
    evidence.add_fact(fact);
    evidence.facts.push_back("[" + fact + "] " +
                              output.substr(0, 200));

    // Track whether tool produced meaningful results (separate from attempt)
    bool has_results = false;
    if (tc.tool == "grep") {
      has_results = !output.empty() && output != "no matches";
      result.recovery_metrics.grep_attempts++;
      if (has_results) {
        result.recovery_metrics.grep_success++;
      } else {
        result.recovery_metrics.grep_zero_hit++;
      }
    } else if (tc.tool == "read") {
      has_results = !output.empty() && output != "no files to read";
      result.recovery_metrics.read_attempts++;
      if (has_results) {
        result.recovery_metrics.read_success++;
      }
    } else {
      has_results = !output.empty();
    }
    if (has_results)
      evidence.add_fact(tc.tool + ":results");

    // Mark evidence class based on tool (only when results produced)
    if (has_results) {
      if (tc.tool == "grep")
        evidence.mark_evidence_class(EvidenceClass::FileSearch);
      else if (tc.tool == "read")
        evidence.mark_evidence_class(EvidenceClass::FileContent);
      else if (tc.tool == "discovery")
        evidence.mark_evidence_class(EvidenceClass::Discovery);
      else if (tc.tool == "cmake" && output.find("error") == std::string::npos)
        evidence.mark_evidence_class(EvidenceClass::Build);
      else if (tc.tool == "ctest" &&
               output.find("failed") == std::string::npos &&
               output.find("FAILED") == std::string::npos)
        evidence.mark_evidence_class(EvidenceClass::Test);
      else if (tc.tool == "gh")
        evidence.mark_evidence_class(EvidenceClass::CIWorkflow);
      else if (tc.tool == "git")
        evidence.mark_evidence_class(EvidenceClass::GitLog);
    }

    // Evaluate confidence after each tool run
    ConfidenceResult cr;
    if (tc.tool == "grep") {
      int hits = 0;
      std::istringstream ss(output);
      std::string line;
      while (std::getline(ss, line))
        if (!line.empty() && line != "no matches")
          hits++;
      result.recovery_metrics.grep_total_hits += hits;
      if (hits > result.recovery_metrics.grep_max_hits)
        result.recovery_metrics.grep_max_hits = hits;
      cr = ConfidenceService::after_search(tc.args, hits);
    } else if (tc.tool == "read") {
      cr = ConfidenceService::after_read(1, true);
    } else if (tc.tool == "cmake") {
      bool ok = output.find("error") == std::string::npos;
      cr = ConfidenceService::after_build(ok, ok ? "" : output.substr(0, 200));
    } else if (tc.tool == "ctest") {
      bool ok = (output.find("failed") == std::string::npos &&
                 output.find("FAILED") == std::string::npos);
      cr.score = ok ? 0.9 : 0.3;
      cr.reason = ok ? "tests passed" : "tests failed";
    } else {
      cr.score = 0.5;
      cr.reason = "tool executed";
    }

    confidence_history.push_back(cr);

    // Check if confidence is too low to continue
    ConfidenceResult combined = ConfidenceService::combine(confidence_history);
    if (ConfidenceService::should_stop(combined, 0.2)) {
      result.stopped_early = true;
      result.stop_reason = "confidence too low: " + combined.reason;
      break;
    }
  }

  // Build summary
  std::string summary;
  summary += "Goal type: " + goal_type_name(type) + "\n";
  summary += "Evidence collected: " + std::to_string(evidence.facts.size()) +
             " facts\n";
  for (auto &f : evidence.facts) {
    // Skip internal tracking facts
    if (f.find(":results") != std::string::npos)
      continue;
    if (f.size() > 100)
      summary += "  " + f.substr(0, 100) + "...\n";
    else
      summary += "  " + f + "\n";
  }

  // Final confidence
  ConfidenceResult final_confidence = ConfidenceService::combine(confidence_history);
  summary += "Confidence: " + final_confidence.reason + "\n";

  result.success = check_completion(goal, type, evidence);
  result.summary = summary;
  result.evidence = std::move(evidence);
  result.goal_type = static_cast<int>(type);
  result.confidence = final_confidence.score;

  // Recovery metrics (compute before outcome logic)
  result.recovery_metrics.attempts = iteration_count;
  result.recovery_metrics.evidence_found =
      result.evidence.has_fact_containing("grep:results") ||
      result.evidence.has_fact_containing("read:results") ||
      result.evidence.has_fact_containing("git:results") ||
      result.evidence.has_fact_containing("build") ||
      result.evidence.has_fact_containing("test");
  result.recovery_metrics.verification_found =
      result.evidence.has_fact_containing("build") ||
      result.evidence.has_fact_containing("test");
  result.recovery_metrics.confidence_delta =
      final_confidence.score - (confidence_history.empty() ? 0.0
                                : confidence_history.front().score);

  // Determine outcome
  if (result.stopped_early) {
    result.outcome = Core::Outcome::InsufficientEvidence;
  } else if (result.success) {
    result.outcome = Core::Outcome::Success;
  } else if (result.recovery_metrics.evidence_found) {
    // Evidence exists but is the wrong class — judgment worked
    result.outcome = Core::Outcome::InsufficientEvidence;
  } else {
    result.outcome = Core::Outcome::Failure;
  }

  return result;
}

} // namespace Services
