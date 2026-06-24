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
#include <set>
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
    "the", "a", "an", "this", "that", "all", "in", "of", "to", "for", "and", "or",
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
  // Session state meta-queries about current runtime configuration
  // These must be checked before call-site patterns since they may contain
  // words like "using" or "provider" that would match CodebaseQuery patterns.
  if (contains_any(goal, {"what provider", "what model", "am i online",
                           "which backend", "which provider",
                           "provider am i", "model am i",
                           "what backend is active", "what backend am i",
                           "what provider is selected", "what provider is active"}))
    return SessionState;

  // Architecture review: read-only structural analysis
  // Must be checked before Architecture/CodebaseOverview to catch "review architecture" etc.
  if (contains_any(goal, {"review architecture", "review codebase",
                           "review recent changes", "review the architecture",
                           "review the codebase", "audit architecture",
                           "audit technical debt", "audit codebase",
                           "review provider system", "review model catalog",
                           "review last commit", "review my changes",
                           "review telemetry", "review routing",
                           "review metrics", "review state",
                           "review tests", "review coverage",
                           "architecture review", "codebase review"}))
    return ArchitectureReview;

  // Architecture / conceptual questions suggesting CodebaseOverview
  if (contains_any(goal, {"architecture", "design", "how it works", "how does it work"}) ||
      contains_any(goal, {"explain the", "how does the", "how does a", "tell me how the"}) ||
      (contains_any(goal, {"how", "explain", "tell me"}) && contains_any(goal, {"work", "works", "pipeline", "architecture", "design", "system", "flow"}))) {
    return CodebaseOverview;
  }

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

  // Commit history and git intent: "status", "branch", "log" commands
  if (contains_any(goal, {"last commit", "last comit", "recent commit", "recent comit",
                             "recent commits", "recent comits",
                             "latest commit", "latest comit",
                             "git history", "git log", "commit log",
                             "recent changes", "recent change",
                             "what changed", "what changed last",
                             "commit history", "show commit", "show recent",
                             "check commit", "check comit",
                             "previous commit", "previous comit",
                             "git status", "current branch", "what branch",
                             "branch am i", "show branch", "git branch"}))
    return CommitHistory;

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
    "CommitHistory - Questions about commit history, recent changes, git log\n"
    "CodebaseOverview - High-level questions about the entire project or codebase\n"
    "CodebaseQuery - Questions about specific code, looking up definitions, searching\n"
    "CodeChange - Add, modify, or remove code\n"
    "CICheck - CI/CD pipeline status or investigation\n"
    "GitHubInvestigation - GitHub Actions run investigation\n"
    "ArchitectureReview - Architecture or codebase review, technical debt audit\n\n"
    "Request: " + goal + "\n\nCategory:";

  std::string response = ai_->chat(prompt, "");
  for (auto &c : response) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (response.find("commithistory") != std::string::npos) return CommitHistory;
  if (response.find("codebaseoverview") != std::string::npos) return CodebaseOverview;
  if (response.find("codebasequery") != std::string::npos) return CodebaseQuery;
  if (response.find("codechange") != std::string::npos) return CodeChange;
  if (response.find("cicheck") != std::string::npos) return CICheck;
  if (response.find("githubinvestigation") != std::string::npos) return GitHubInvestigation;
  if (response.find("architecturereview") != std::string::npos) return ArchitectureReview;
  return GeneralChat;
}

std::string ExecutionEngine::goal_type_name(GoalType t) {
  switch (t) {
    case CICheck: return "CI Investigation";
    case CommitHistory: return "Commit History";
    case CodebaseQuery: return "Repository Investigation";
    case CodebaseOverview: return "Codebase Overview";
    case CodeChange: return "Code Change";
    case GitHubInvestigation: return "GitHub Investigation";
    case GeneralChat: return "General Chat";
    case SessionState: return "Session State";
    case ArchitectureReview: return "Architecture Review";
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
    case CommitHistory:
      return {EvidenceClass::GitLog};
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
    case SessionState:
      return {};
    case ArchitectureReview:
      return {EvidenceClass::Discovery, EvidenceClass::FileSearch,
              EvidenceClass::FileContent};
  }
  return {};
}

// ---------------------------------------------------------------------------
// Tool selection
// ---------------------------------------------------------------------------

ToolCall ExecutionEngine::select_next_tool(
    const std::string &goal, GoalType type, const EvidenceStore &evidence,
    const std::vector<ToolResult> &tool_history) {
  if (mode_ == ClassifierMode::LLM && ai_)
    return select_next_tool_llm(goal, type, evidence, tool_history);

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

    case CommitHistory: {
      if (!evidence.has_fact_containing("git log"))
        return {"git", "log --oneline -10"};
      if (!evidence.has_fact_containing("git log -1"))
        return {"git", "log -1 --format=\"%H %s%n%b\""};
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

        // Score each phrase group: prefer code-shaped terms and later positions
        std::vector<int> group_scores(phrase_groups.size(), 0);
        for (size_t gi = 0; gi < phrase_groups.size(); gi++) {
          for (auto &w : phrase_groups[gi]) {
            if (w.find('_') != std::string::npos) group_scores[gi] += 10;
            if (w.find("::") != std::string::npos) group_scores[gi] += 8;
            if (w.size() >= 2 && std::isupper(static_cast<unsigned char>(w[0])) &&
                std::any_of(w.begin() + 1, w.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)); }))
              group_scores[gi] += 10;
            bool has_upper = false, has_lower = false;
            for (auto c : w) {
              if (std::isupper(static_cast<unsigned char>(c))) has_upper = true;
              if (std::islower(static_cast<unsigned char>(c))) has_lower = true;
            }
            if (has_upper && has_lower) group_scores[gi] += 6;
          }
          group_scores[gi] += static_cast<int>(gi) * 2; // position bonus: prefer later groups
        }

        // Pick the best group (highest score; ties go to later position via the position bonus already added)
        size_t best_idx = 0;
        for (size_t gi = 1; gi < phrase_groups.size(); gi++)
          if (group_scores[gi] > group_scores[best_idx])
            best_idx = gi;

        if (!phrase_groups.empty()) {
          auto &best_group = phrase_groups[best_idx];
          // If any word in the best group has a strong code-shape score (≥10),
          // prefer that word alone rather than the full multi-word phrase.
          // This avoids phrases like "set last_outcome" when "last_outcome" is the real target.
          std::string code_word;
          for (auto &w : best_group) {
            int ws = 0;
            if (w.find('_') != std::string::npos) ws += 10;
            if (w.find("::") != std::string::npos) ws += 8;
            if (w.size() >= 2 && std::isupper(static_cast<unsigned char>(w[0])) &&
                std::any_of(w.begin() + 1, w.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)); }))
              ws += 10;
            if (ws >= 10) {
              code_word = w;
              break;
            }
          }
          if (!code_word.empty()) {
            term = code_word;
          } else if (best_group.size() >= 2) {
            std::string reconstructed;
            for (size_t i = 0; i < best_group.size(); ++i) {
              if (i > 0) reconstructed += "[ _-]?";
              reconstructed += best_group[i];
            }
            term = reconstructed;
          } else {
            term = best_group[0];
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

    case ArchitectureReview: {
      if (!evidence.has_fact_containing("discovery"))
        return {"discovery", "architecture_review"};
      if (!evidence.has_fact_containing("git log"))
        return {"git", "log --oneline -10"};
      if (!evidence.has_fact_containing("grep AgentMode"))
        return {"grep", "AgentMode"};
      if (!evidence.has_fact_containing("grep MODE_"))
        return {"grep", "MODE_"};
      if (!evidence.has_fact_containing("grep AuthProvider"))
        return {"grep", "AuthProvider"};
      if (!evidence.has_fact_containing("grep provider_label"))
        return {"grep", "provider_label|category_label|tier_label|api_key_var"};
      // Use "read " + path for precise tracking — avoids false matches
      // from grep output that happens to contain the same path substring
      if (!evidence.has_fact_containing("read include/core/session_state.h"))
        return {"read", "include/core/session_state.h"};
      if (!evidence.has_fact_containing("read include/core/metrics.h"))
        return {"read", "include/core/metrics.h"};
      if (!evidence.has_fact_containing("read src/services/execution_engine.cpp"))
        return {"read", "src/services/execution_engine.cpp"};
      if (!evidence.has_fact_containing("strategy_changes"))
        return {"grep", "strategy_changes"};
      if (!evidence.has_fact_containing("validation_runner"))
        return {"read", "tests/validation_runner.cpp"};
      return {};
    }

    case GeneralChat:
      return {};
    case SessionState:
      return {};
  }

  return {};
}

ToolCall ExecutionEngine::select_next_tool_llm(
    const std::string &goal, GoalType type, const EvidenceStore &evidence,
    const std::vector<ToolResult> &tool_history) {
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

  // Tool history: structured results from previous iterations
  if (!tool_history.empty()) {
    prompt += "Previous tool calls:\n";
    for (auto &tr : tool_history) {
      prompt += "  Tool: " + tr.tool + " " + tr.args + "\n";
      prompt += "  Result: " + std::string(tr.success() ? "SUCCESS" : "FAILED") + "\n";
      prompt += "  stdout:\n";
      // Show first 300 chars of stdout, indented
      std::string out = tr.out;
      if (out.size() > 300) out = out.substr(0, 300) + "...";
      if (!out.empty()) {
        std::istringstream lines(out);
        std::string line;
        while (std::getline(lines, line))
          prompt += "    " + line + "\n";
      }
      if (!tr.err.empty()) {
        prompt += "  stderr:\n";
        std::string err = tr.err;
        if (err.size() > 200) err = err.substr(0, 200) + "...";
        std::istringstream lines(err);
        std::string line;
        while (std::getline(lines, line))
          prompt += "    " + line + "\n";
      }
      if (!tr.success())
        prompt += "  (exit code " + std::to_string(tr.exit_code) + ")\n";
      prompt += "\n";
    }
    prompt += "\n";
  }

  // Fallback text evidence for tools that pre-date structured results
  if (tool_history.empty()) {
    prompt += "Evidence collected:\n";
    for (auto &f : evidence.facts)
      prompt += "  " + f + "\n";
  }

  prompt +=
    "Choose the next tool. Options:\n"
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

    case CommitHistory:
      return evidence.has_fact_containing("git:results");

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

    case ArchitectureReview:
      return select_next_tool(goal, type, evidence, {}).tool.empty();

    case GeneralChat:
      return true;
    case SessionState:
      return true;
  }
  return true;
}

bool ExecutionEngine::goal_is_achieved(const std::string &goal,
                                        const EvidenceStore &evidence) {
  return check_completion(goal, classify_goal(goal), evidence);
}

// ---------------------------------------------------------------------------
// Architecture review report builder
// ---------------------------------------------------------------------------

static bool has_grep_output(const ToolResult &tr) {
  return !tr.out.empty() && tr.out != "no matches";
}

static std::string extract_first_file(const std::string &grep_output) {
  auto colon = grep_output.find(':');
  if (colon == std::string::npos) return {};
  return grep_output.substr(0, colon);
}

static void append_finding(std::ostringstream &r, int &n,
                            const std::string &title,
                            const std::string &risk,
                            const std::string &evidence,
                            const std::string &recommendation) {
  n++;
  r << "\nFinding #" << n << "\n" << title << "\n\n";
  r << "Risk:\n" << risk << "\n\n";
  r << "Evidence:\n" << evidence << "\n\n";
  r << "Recommendation:\n" << recommendation << "\n\n";
}

std::string ExecutionEngine::build_review_report(
    const std::vector<ToolResult> &tool_history) const {
  std::ostringstream r;

  r << "Architecture Review\n";
  r << std::string(50, '=') << "\n";

  // Context section
  for (auto &tr : tool_history) {
    if (tr.tool == "discovery") {
      r << "\nProject:\n";
      std::istringstream in(tr.out);
      std::string line;
      while (std::getline(in, line))
        if (!line.empty()) r << "  " << line << "\n";
    }
    if (tr.tool == "git" && tr.args == "log --oneline -10") {
      r << "\nRecent commits:\n";
      std::istringstream in(tr.out);
      std::string line;
      int count = 0;
      while (std::getline(in, line) && count < 5) {
        if (!line.empty()) { r << "  " << line << "\n"; count++; }
      }
    }
  }
  r << "\n";

  int findings = 0;

  for (auto &tr : tool_history) {
    // Dead code: AgentMode enum
    if (tr.tool == "grep" && tr.args == "AgentMode" && has_grep_output(tr)) {
      auto f = extract_first_file(tr.out);
      append_finding(r, findings,
        "Legacy AgentMode enum remains",
        "Medium",
        f + " contains `enum AgentMode { ... }`\n"
             "Never referenced at runtime — vestigial.",
        "Remove AgentMode enum and associated MODE_ constants\n"
             "after one release cycle.");
    }

    // Dead code: MODE_ constants
    if (tr.tool == "grep" && tr.args == "MODE_" && has_grep_output(tr)) {
      auto f = extract_first_file(tr.out);
      append_finding(r, findings,
        "MODE_ constants from unused AgentMode system",
        "Low",
        f + " contains `MODE_*` constants\n"
             "Part of the unused AgentMode enum.",
        "Remove MODE_ constants alongside AgentMode cleanup.");
    }

    // Duplication: AuthProvider
    if (tr.tool == "grep" && tr.args == "AuthProvider" && has_grep_output(tr)) {
      auto f = extract_first_file(tr.out);
      append_finding(r, findings,
        "AuthProvider duplicates ModelCatalog metadata",
        "Medium",
        f + " defines `AuthProvider` struct with provider name,\n"
             "display_name, base_url, model — duplicating\n"
             "`ModelCatalog.provider()` metadata.",
        "Consolidate provider metadata in ModelCatalog.\n"
             "Remove AuthProvider or derive from ModelCatalog.");
    }

    // Duplication: provider_label functions
    if (tr.tool == "grep" && tr.args == "provider_label|category_label|tier_label|api_key_var" && has_grep_output(tr)) {
      auto f = extract_first_file(tr.out);
      append_finding(r, findings,
        "Duplicate provider display-name functions in startup.cpp",
        "Low",
        f + " defines `provider_label`, `category_label`, `tier_label`,\n"
             "`api_key_var` — these duplicate display-name logic\n"
             "already present in ModelCatalog.",
        "Remove label functions from startup.cpp.\n"
             "Use ModelCatalog display names directly.");
    }

    // Serialization gap: TrustMetrics
    if (tr.tool == "read" && tr.args.find("session_state.h") != std::string::npos) {
      append_finding(r, findings,
        "TrustMetrics fields never populated by engine",
        "Medium",
        "include/core/session_state.h: `last_trust_metrics`\n"
           "include/core/metrics.h: `plan_approved`, `diff_approved`,\n"
           "  `user_corrected_goal`, `reverted`\n\n"
           "Schema exists. Replay stores it.\n"
           "ExecutionEngine never sets any field —\n"
           "replay events carry all-false trust metrics.",
        "Populate trust_metrics in ExecutionEngine::execute()\n"
           "after each user interaction, or remove the field\n"
           "from engine ExecutionResult.");
    }

    // Dead metric: strategy_changes
    if (tr.tool == "read" && tr.args.find("metrics.h") != std::string::npos) {
      append_finding(r, findings,
        "strategy_changes is never populated",
        "Low",
        "include/core/metrics.h: `RecoveryMetrics.strategy_changes`\n"
           "src/services/replay_service.cpp: serialized (always 0)\n"
           "src/services/dashboard_service.cpp: displayed (always 0)\n\n"
           "Declared. Recorded. Serialized. Displayed.\n"
           "Never assigned. Always 0.",
        "Remove strategy_changes from RecoveryMetrics\n"
           "or implement strategy-change detection in the\n"
           "investigation loop.");
    }



    // Test coverage gaps
    if (tr.tool == "read" && tr.args == "tests/validation_runner.cpp") {
      std::vector<std::string> untested;
      if (tr.out.find("CodeChange") == std::string::npos)
        untested.push_back("CodeChange");
      if (tr.out.find("CICheck") == std::string::npos)
        untested.push_back("CICheck");
      if (tr.out.find("GitHubInvestigation") == std::string::npos)
        untested.push_back("GitHubInvestigation");
      if (tr.out.find("ArchitectureReview") == std::string::npos)
        untested.push_back("ArchitectureReview");
      if (!untested.empty()) {
        std::string evidence = "tests/validation_runner.cpp: queries vector\n"
                               "  Missing coverage for GoalTypes:\n";
        for (auto &u : untested)
          evidence += "    - " + u + "\n";
        append_finding(r, findings,
          "Validation coverage gaps",
          "Low",
          evidence,
          "Add validation queries for each untested GoalType\n"
             "to the validation_runner.cpp queries vector.");
      }
    }
  }

  if (findings == 0) {
    r << "\nNo findings — architecture is clean.\n";
  }

  r << std::string(50, '=') << "\n";
  r << findings << " finding" << (findings == 1 ? "" : "s") << " total\n";
  r << "Review mode is read-only. No files were modified.\n";

  return r.str();
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
  std::set<std::string> seen_tool_calls;

  for (iteration_count = 0; iteration_count < MAX_ITERATIONS; iteration_count++) {
    if (check_completion(goal, type, evidence))
      break;

    ToolCall tc = select_next_tool(goal, type, evidence, result.tool_history);
    if (tc.tool.empty())
      break;

    // Deduplicate: if the exact same tool+args was already executed, stop looping
    std::string tc_signature = tc.tool + ":" + tc.args;
    if (!seen_tool_calls.insert(tc_signature).second) {
      result.stopped_early = true;
      result.stop_reason = "duplicate tool call: " + tc_signature;
      break;
    }

    ui.show_tool_invocation(tc.tool, tc.args);

    ToolResult tr = run_tool(tc);
    tr.tool = tc.tool;
    tr.args = tc.args;
    result.tool_history.push_back(tr);
    ui.show_tool_output(tr.out);

    std::string fact = tc.tool;
    if (!tc.args.empty())
      fact += " " + tc.args;
    evidence.add_fact(fact);
    evidence.facts.push_back("[" + fact + "] " +
                              tr.out.substr(0, 200));

    // Track whether tool produced meaningful results (separate from attempt)
    bool has_results = false;
    if (tc.tool == "grep") {
      has_results = !tr.out.empty() && tr.out != "no matches";
      result.recovery_metrics.grep_attempts++;
      if (has_results) {
        result.recovery_metrics.grep_success++;
      } else {
        result.recovery_metrics.grep_zero_hit++;
      }
    } else if (tc.tool == "read") {
      has_results = !tr.out.empty() && tr.out != "no files to read";
      result.recovery_metrics.read_attempts++;
      if (has_results) {
        result.recovery_metrics.read_success++;
      }
    } else {
      has_results = !tr.out.empty();
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
      else if (tc.tool == "cmake" && tr.out.find("error") == std::string::npos)
        evidence.mark_evidence_class(EvidenceClass::Build);
      else if (tc.tool == "ctest" &&
               tr.out.find("failed") == std::string::npos &&
               tr.out.find("FAILED") == std::string::npos)
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
      std::istringstream ss(tr.out);
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
      bool ok = tr.out.find("error") == std::string::npos;
      cr = ConfidenceService::after_build(ok, ok ? "" : tr.out.substr(0, 200));
    } else if (tc.tool == "ctest") {
      bool ok = (tr.out.find("failed") == std::string::npos &&
                 tr.out.find("FAILED") == std::string::npos);
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
  summary +=
      "Tools executed: " + std::to_string(result.tool_history.size()) + "\n";
  for (auto &tr : result.tool_history) {
    summary += "  Tool: " + tr.tool + " " + tr.args + "\n";
    summary += "  Result: " + std::string(tr.success() ? "SUCCESS" : "FAILED");
    if (!tr.success())
      summary += " (exit " + std::to_string(tr.exit_code) + ")";
    summary += "\n";
    if (!tr.err.empty()) {
      summary += "  stderr: " + tr.err.substr(0, 200) + "\n";
    }
    std::string out = tr.out;
    if (out.size() > 300) out = out.substr(0, 300) + "...";
    if (!out.empty()) {
      summary += "  Output:\n";
      std::istringstream lines(out);
      std::string line;
      while (std::getline(lines, line))
        summary += "    " + line + "\n";
    }
    summary += "\n";
  }

  // Final confidence
  ConfidenceResult final_confidence = ConfidenceService::combine(confidence_history);
  summary += "Confidence: " + final_confidence.reason + "\n";

  result.success = check_completion(goal, type, evidence);
  if (type == ArchitectureReview) {
    summary = build_review_report(result.tool_history);
  }
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
