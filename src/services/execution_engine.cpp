#include "services/execution_engine.h"
#include "services/evidence_gap_engine.h"
#include "services/planner.h"
#include "core/investigation_session.h"
#include "services/ai_service.h"
#include "services/confidence_service.h"
#include "ui/ui_manager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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

// Extract the best single search term from a natural-language query.
// Uses phrase scoring, code-shape detection, and stop-word filtering.
std::string extract_best_term(const std::string &raw_goal) {
  std::string term = raw_goal;

  // Extract quoted terms if present
  size_t first_quote = term.find('"');
  size_t last_quote = term.rfind('"');
  if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
    return term.substr(first_quote + 1, last_quote - first_quote - 1);
  }
  first_quote = term.find('\'');
  last_quote = term.rfind('\'');
  if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
    return term.substr(first_quote + 1, last_quote - first_quote - 1);
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

  if (phrase_groups.empty())
    return "";

  // Pick the best group
  size_t best_idx = 0;
  for (size_t gi = 1; gi < phrase_groups.size(); gi++)
    if (group_scores[gi] > group_scores[best_idx])
      best_idx = gi;

  auto &best_group = phrase_groups[best_idx];
  // If any word in the best group has a strong code-shape score (>=10),
  // prefer that word alone rather than the full multi-word phrase.
  for (auto &w : best_group) {
    int ws = 0;
    if (w.find('_') != std::string::npos) ws += 10;
    if (w.find("::") != std::string::npos) ws += 8;
    if (w.size() >= 2 && std::isupper(static_cast<unsigned char>(w[0])) &&
        std::any_of(w.begin() + 1, w.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)); }))
      ws += 10;
    if (ws >= 10)
      return w;
  }
  // No code-shaped word: return the first noun (most distinctive)
  if (best_group.size() >= 2)
    return best_group[0];
  return best_group[0];
}

// Determine if a query is asking about implementation/definition location.
bool is_implementation_query(const std::string &goal) {
  std::string lower = goal;
  for (auto &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower.find("implemented") != std::string::npos ||
         lower.find("implementation") != std::string::npos ||
         lower.find("defined") != std::string::npos ||
         lower.find("where is") != std::string::npos;
}

bool is_git_diff_query(const std::string &goal) {
  std::string lower = goal;
  for (auto &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower.find("git diff") != std::string::npos ||
         lower.find("show diff") != std::string::npos ||
         lower.find("what diff") != std::string::npos;
}

bool is_reference_query(const std::string &goal) {
  std::string lower = goal;
  for (auto &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower.find("referenced") != std::string::npos ||
         lower.find("reference") != std::string::npos ||
         lower.find("references") != std::string::npos ||
         lower.find("calls") != std::string::npos ||
         lower.find("call") != std::string::npos ||
         lower.find("called") != std::string::npos ||
         lower.find("uses") != std::string::npos ||
         lower.find("using") != std::string::npos ||
         lower.find("used") != std::string::npos ||
         lower.find("use") != std::string::npos ||
         lower.find("owns") != std::string::npos ||
         lower.find("ownership") != std::string::npos ||
         lower.find("depends") != std::string::npos ||
         lower.find("dependency") != std::string::npos ||
         lower.find("dependencies") != std::string::npos;
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

void EvidenceStore::add_evidence_entry(EvidenceClass ec,
                                       const std::string &tool,
                                       const std::string &query,
                                       EvidenceQuality quality,
                                       EvidenceStrength strength,
                                       int match_count,
                                       int exact_match_count,
                                       bool phrase_match) {
  // Allow multiple entries from different tools even if quality is already met,
  // so the EvidenceGapEngine can detect independent verification.
  // Only skip if the exact same tool already covered this at >= quality.
  if (!tool.empty()) {
    for (auto &e : entries)
      if (e.type == ec && e.tool == tool && e.quality >= quality)
        return;
  } else {
    // No tool specified: fall back to old dedup (any entry of this type)
    if (has_quality(ec, quality))
      return;
  }
  EvidenceEntry ee;
  ee.type = ec;
  ee.quality = quality;
  ee.strength = strength;
  ee.tool = tool;
  ee.query = query;
  ee.target = query; // simplified: use query as target
  ee.match_count = match_count;
  ee.exact_match_count = exact_match_count;
  ee.phrase_match = phrase_match;
  entries.push_back(ee);
}

void EvidenceStore::mark_evidence_class(EvidenceClass ec) {
  // Legacy: add entry with Weak quality (equivalent to old binary model)
  add_evidence_entry(ec, "", "", Weak, Low, 0, 0, false);
}

bool EvidenceStore::has_quality(EvidenceClass ec, EvidenceQuality min_quality) const {
  for (auto &e : entries) {
    if (e.type == ec && e.quality >= min_quality)
      return true;
  }
  return false;
}

bool EvidenceStore::has_any_evidence_class(const std::vector<EvidenceClass> &required) const {
  for (auto &req : required)
    for (auto &e : entries)
      if (e.type == req)
        return true;
  return false;
}

bool EvidenceStore::has_all_evidence_classes(const std::vector<EvidenceClass> &required) const {
  for (auto &req : required) {
    bool found = false;
    for (auto &e : entries)
      if (e.type == req) { found = true; break; }
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
  if (contains_any(goal, {"architecture", "design", "designed", "how it works", "how does it work"}) ||
      contains_any(goal, {"explain the", "how does the", "how does a", "tell me how the"}) ||
      (contains_any(goal, {"how", "explain", "tell me"}) && contains_any(goal, {"work", "works", "pipeline", "architecture", "design", "designed", "system", "flow"}))) {
    return CodebaseOverview;
  }

  // Call-site, usage, definition queries strongly suggest codebase query even if they mention CI/external command keywords
  if (contains_any(goal, {"call", "calls", "called", "use", "uses", "used", "using", "implement", "implemented",
                           "define", "defined", "reference", "references", "referenced",
                           "where is", "where do we", "where are",
                           "what owns", "who owns", "owned by",
                           "depends on", "dependency", "dependencies"})) {
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
                              "changed files", "files changed",
                              "modified files", "what files changed",
                              "show changed files", "show modified files",
                              "check changed files", "check modified files",
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

  // Catch-all: queries that escaped investigation-specific patterns but
  // clearly ask about repository state. Prevents silent GeneralChat fallback
  // where the AI synthesizes answers without running any tools.
  if (contains_any(goal, {"check the", "check what", "check if",
                           "check whether", "check for",
                           "what are the", "what is the current",
                           "get the latest", "get the current",
                           "list all", "list the"}))
    return CodebaseQuery;

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

  std::string response = ai_->chat(prompt, "", "You are a goal classification assistant. Your role is to classify the request into exactly one category name from the list.");
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
                            "changed files", "files changed",
                            "modified files", "what files changed",
                            "show changed files", "show modified files",
                            "check changed files", "check modified files",
                            "previous version", "recent change"}))
    return EvidenceNeed::CommitHistory;
  return EvidenceNeed::Default;
}

std::vector<EvidenceRequirement> ExecutionEngine::evidence_for_goal(const Goal &goal) {
  if (!goal.is_known())
    return {};

  // Helper to build a requirement
  auto req = [](EvidenceClass ec, EvidenceQuality min_q) {
    return EvidenceRequirement{ec, min_q};
  };

  switch (goal.intent) {
    case Intent::Status:
      if (goal.entity == Entity::Session)
        return {};
      return {req(EvidenceClass::GitLog, Weak)};  // any git output suffices

    case Intent::Locate:
    case Intent::Navigate:
      return {req(EvidenceClass::FileSearch, Moderate),
              req(EvidenceClass::FileContent, Moderate)};

    case Intent::Explain:
      return {req(EvidenceClass::Discovery, Weak),
              req(EvidenceClass::FileContent, Moderate)};

    case Intent::Review:
      return {req(EvidenceClass::Discovery, Weak),
              req(EvidenceClass::FileSearch, Moderate),
              req(EvidenceClass::FileContent, Moderate)};

    case Intent::Diagnose:
      if (goal.entity == Entity::CIPipeline ||
          goal.entity == Entity::GitHubAction)
        return {req(EvidenceClass::CIWorkflow, Moderate)};
      if (goal.entity == Entity::Build)
        return {req(EvidenceClass::Build, Weak)};
      if (goal.entity == Entity::Test)
        return {req(EvidenceClass::Test, Weak)};
      return {req(EvidenceClass::FileSearch, Moderate),
              req(EvidenceClass::FileContent, Moderate)};

    case Intent::Compare:
      return {req(EvidenceClass::FileSearch, Moderate),
              req(EvidenceClass::FileContent, Moderate)};

    case Intent::Modify:
      return {req(EvidenceClass::Discovery, Weak),
              req(EvidenceClass::FileSearch, Moderate),
              req(EvidenceClass::FileContent, Moderate),
              req(EvidenceClass::Build, Moderate),
              req(EvidenceClass::Test, Moderate)};

    case Intent::Execute:
      return {req(EvidenceClass::Build, Weak),
              req(EvidenceClass::Test, Weak)};

    case Intent::Chat:
      return {};

    default:
      return {};
  }
}

bool ExecutionEngine::check_completion_goal(const Goal &goal,
                                             EvidenceStore &evidence) {
  // Chat and Unknown require no evidence
  if (goal.intent == Intent::Chat || goal.intent == Intent::Unknown)
    return true;

  // Session state queries require no evidence for meta intents only
  if (goal.entity == Entity::Session &&
      (goal.intent == Intent::Chat || goal.intent == Intent::Unknown))
    return true;

  auto needed = evidence_for_goal(goal);
  if (needed.empty())
    return true;

  // Check each requirement at its minimum quality threshold
  for (auto &req : needed) {
    if (!evidence.has_quality(req.ec, req.min_quality))
      return false;
  }
  return true;
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
    const std::string &goal, GoalType type, EvidenceStore &evidence,
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
      // Handle git status queries separately from git log
      if (contains_any(goal, {"git status", "status", "what changed",
                              "changed files", "what files changed",
                              "working tree", "uncommitted"})) {
        if (!evidence.has_fact_containing("git status"))
          return {"git", "status"};
        return {};
      }
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

      // 0. Git diff (before reference/find -- live operation, not file search)
      if (is_git_diff_query(goal)) {
        if (!evidence.has_fact_containing("git diff")) {
          return {"git", "diff"};
        }
        return {};
      }

      // 0. Reference Search (before find lookup)
      if (is_reference_query(goal)) {
        if (!evidence.has_fact_containing("references:done")) {
          std::string term = extract_best_term(goal);
          if (!term.empty()) {
            return {"references", term};
          }
          evidence.add_fact("references:done");
          evidence.add_fact("references:noresults");
        }
        if (evidence.has_fact_containing("references:results") &&
            !evidence.has_fact_containing("read")) {
          return {"read", ""};
        }
        return {};
      }

      // 0. Directory-aware find lookup (before grep fallback)
      if (!evidence.has_fact_containing("find:done")) {
        std::string term = extract_best_term(goal);
        if (!term.empty()) {
          bool impl = is_implementation_query(goal);
          // Signal implementation-vs-header preference via args
          return {"find", term + (impl ? " --impl" : "")};
        }
        // Empty term -- skip find, go straight to grep
        evidence.add_fact("find:done");
        evidence.add_fact("find:noresults");
      }

      // 0b. If find returned results, read the top file(s)
      if (evidence.has_fact_containing("find:results") &&
          !evidence.has_fact_containing("read")) {
        return {"read", ""};
      }

      // 1. Search the codebase (grep fallback -- only if find didn't resolve)
      if (!evidence.has_fact_containing("grep") &&
          !evidence.has_fact_containing("search")) {
        std::string term = extract_best_term(goal);
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
      // Use "read " + path for precise tracking -- avoids false matches
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
      for (auto &e : evidence.entries)
        if (e.type == ec) { have_it = true; break; }
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

  std::string response = ai_->chat(prompt, "", "You are a repository investigation assistant. Your role is to select the next tool to run based on the goal type, user request, required evidence, and history of tool calls.");
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
// Recovery tool selection -- called when the primary sequence is exhausted
// but evidence is still insufficient.
// ---------------------------------------------------------------------------

ToolCall ExecutionEngine::select_recovery_tool(
    const std::string &goal, GoalType type, EvidenceStore &evidence,
    const std::vector<ToolResult> &tool_history) {
  (void)tool_history;

  // Strategy 1: find found nothing → try grep
  if (evidence.has_fact_containing("find:noresults") &&
      !evidence.has_fact_containing("grep:results") &&
      !evidence.has_fact_containing("grep:noresults")) {
    std::string term = extract_best_term(goal);
    if (!term.empty())
      return {"grep", term};
  }

  // Strategy 2: grep returned results but nothing was read → read now
  if (evidence.has_fact_containing("grep:results") &&
      !evidence.has_fact_containing("read")) {
    return {"read", ""};
  }

  // Strategy 3: find found something and we read, but still insufficient →
  // try grep with the same term to find additional references
  if (evidence.has_fact_containing("find:results") &&
      evidence.has_fact_containing("read") &&
      !evidence.has_fact_containing("grep:results") &&
      !evidence.has_fact_containing("grep:noresults") &&
      !evidence.has_fact_containing("recovery:grep_after_find")) {
    std::string term = extract_best_term(goal);
    if (!term.empty())
      return {"grep", term};
  }

  // Strategy 4: nothing found yet → try discovery to understand the project
  if (!evidence.has_fact_containing("discovery") &&
      !evidence.has_fact_containing("recovery:discovery")) {
    return {"discovery", ""};
  }

  // Strategy 5: headers were examined but not implementation files
  // (only applicable when find was involved)
  if (type == CodebaseQuery && evidence.has_fact_containing("find:results")) {
    bool has_header = false;
    bool has_impl = false;
    for (auto &fact : evidence.facts) {
      if (fact.find(".h\"]") != std::string::npos ||
          fact.find(".hpp\"]") != std::string::npos)
        has_header = true;
      if (fact.find(".cpp\"]") != std::string::npos ||
          fact.find(".cc\"]") != std::string::npos ||
          fact.find(".c\"]") != std::string::npos)
        has_impl = true;
    }
    if (has_header && !has_impl &&
        !evidence.has_fact_containing("recovery:find_impl")) {
      std::string term = extract_best_term(goal);
      if (!term.empty())
        return {"find", term + " --impl"};
    }
    if (has_impl && !has_header &&
        !evidence.has_fact_containing("recovery:find_header")) {
      std::string term = extract_best_term(goal);
      if (!term.empty())
        return {"find", term};
    }
  }

  return {};
}

// ---------------------------------------------------------------------------
// Completion check
// ---------------------------------------------------------------------------

bool ExecutionEngine::check_completion(const std::string &goal,
                                        GoalType type,
                                        EvidenceStore &evidence) {
  // If it's a git diff query, complete when git diff result is available.
  if (is_git_diff_query(goal) && evidence.has_fact_containing("git diff")) {
    return true;
  }

  // If it's a reference query and we found no results, we don't need any more evidence.
  if (is_reference_query(goal) && evidence.has_fact_containing("references:noresults")) {
    return true;
  }

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

      // Reference search completion
      if (is_reference_query(goal)) {
        bool refs_ok = evidence.has_fact_containing("references:results") &&
                       evidence.has_fact_containing("read");
        bool refs_noresults = evidence.has_fact_containing("references:noresults");
        return refs_ok || refs_noresults;
      }

      // Complete when find+read resolved, or grep+read resolved
      bool find_ok = evidence.has_fact_containing("find:results") &&
                     evidence.has_fact_containing("read");
      bool grep_ok = evidence.has_fact_containing("grep:results") &&
                     evidence.has_fact_containing("read");
      bool find_tried_and_fell_through =
          evidence.has_fact_containing("find:noresults") &&
          evidence.has_fact_containing("grep:results") &&
          evidence.has_fact_containing("read");
      return find_ok || grep_ok || find_tried_and_fell_through;
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
                                        EvidenceStore &evidence) {
  return check_completion(goal, classify_goal(goal), evidence);
}

// ---------------------------------------------------------------------------
// Architecture review report builder
// ---------------------------------------------------------------------------

static bool has_grep_output(const ToolResult &tr) {
  return !tr.out.empty() && tr.out != "no matches";
}

static void append_finding(std::ostringstream &r, int &n,
                            const std::string &title,
                            const std::string &risk,
                            const std::string &evidence,
                            const std::string &recommendation) {
  n++;
  r << "\n## " << title << "\n\n";
  r << "Risk: " << risk << "\n\n";
  r << "Evidence:\n" << evidence << "\n\n";
  r << "Recommendation:\n" << recommendation << "\n\n";
}

std::string ExecutionEngine::build_review_report(
    const std::vector<ToolResult> &tool_history) const {
  std::ostringstream r;

  r << "Architecture Review Report (read-only)\n";

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
      std::string f;
      std::istringstream iss(tr.out);
      std::string line;
      while (std::getline(iss, line)) {
        if (line.find("enum class AgentMode") != std::string::npos ||
            line.find("enum AgentMode") != std::string::npos) {
          auto colon = line.find(':');
          if (colon != std::string::npos) {
            std::string possible_f = line.substr(0, colon);
            if (possible_f.find("execution_engine") == std::string::npos &&
                possible_f.find("architecture_diff_review") == std::string::npos) {
              f = possible_f;
              break;
            }
          }
        }
      }
      if (!f.empty()) {
        append_finding(r, findings,
          "Legacy AgentMode enum remains",
          "Medium",
          f + " defines `enum AgentMode { ... }`\n"
               "Never referenced at runtime -- vestigial.",
          "Remove AgentMode enum and associated MODE_ constants\n"
               "after one release cycle.");
      }
    }

    // Dead code: MODE_ constants
    if (tr.tool == "grep" && tr.args == "MODE_" && has_grep_output(tr)) {
      std::string f;
      std::istringstream iss(tr.out);
      std::string line;
      while (std::getline(iss, line)) {
        if (line.find(".h:") != std::string::npos || line.find(".hpp:") != std::string::npos) {
          if (line.find("MODE_") != std::string::npos && 
              (line.find("=") != std::string::npos || line.find(",") != std::string::npos)) {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
              std::string possible_f = line.substr(0, colon);
              if (possible_f.find("execution_engine") == std::string::npos &&
                  possible_f.find("architecture_diff_review") == std::string::npos) {
                f = possible_f;
                break;
              }
            }
          }
        }
      }
      if (!f.empty()) {
        append_finding(r, findings,
          "MODE_ constants from unused AgentMode system",
          "Low",
          f + " contains `MODE_*` constants\n"
               "Part of the unused AgentMode enum.",
          "Remove MODE_ constants alongside AgentMode cleanup.");
      }
    }

    // (Removed false-positive duplication checks for AuthProvider and provider_label functions)







    // Test coverage gaps
    if (tr.tool == "read" && tr.args == "tests/validation_runner.cpp") {
      std::vector<std::string> untested;
      if (tr.out.find("add a new") == std::string::npos && tr.out.find("fix compile") == std::string::npos)
        untested.push_back("CodeChange");
      if (tr.out.find("ci build") == std::string::npos && tr.out.find("workflow run") == std::string::npos)
        untested.push_back("CICheck");
      if (tr.out.find("github.com") == std::string::npos && tr.out.find("actions/runs") == std::string::npos && tr.out.find("83332734648") == std::string::npos)
        untested.push_back("GitHubInvestigation");
      if (tr.out.find("review architecture") == std::string::npos && tr.out.find("review codebase") == std::string::npos)
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
    r << "\nNo findings -- architecture is clean.\n";
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

  // Parse goal through GoalUnderstandingService for telemetry comparison
  // (routing still uses classify_goal() -- Goal is not yet used for decisions)
  {
    GoalUnderstandingService gus;
    result.parsed_goal = gus.parse(goal);
  }

  ui.show_pipeline_section(goal_type_name(type));

  // When GoalUnderstandingService is confident (>= 0.5), use Goal-based
  // evidence derivation and completion instead of GoalType-based.
  // This is the first step toward removing keyword routing -- the planner
  // consumes semantic Goal, not raw GoalType.
  const double GOAL_CONFIDENCE_THRESHOLD = 0.5;
  bool use_goal_routing = (result.parsed_goal.confidence >= GOAL_CONFIDENCE_THRESHOLD);

  const int MAX_ITERATIONS = 20;
  const int MAX_RECOVERY = 3;
  const double RECOVERY_THRESHOLD = 0.5; // will be derived from calibration data in step 7
  std::vector<ConfidenceResult> confidence_history;
  int iteration_count = 0;
  int recovery_count = 0;
  std::set<std::string> seen_tool_calls;
  // Use the user's query as the initial search target for convergence detection.
  // When tools have empty args, this provides a fallback target so that
  // multiple tools operating on the same topic can still converge.
  std::string last_search_target = goal;

  for (iteration_count = 0; iteration_count < MAX_ITERATIONS; iteration_count++) {
    if (use_goal_routing
        ? check_completion_goal(result.parsed_goal.goal, evidence)
        : check_completion(goal, type, evidence)) {
      // Completion is satisfied, but try confidence-based recovery if confidence
      // is moderate and a recovery strategy is available
      if (recovery_count < MAX_RECOVERY && !confidence_history.empty()) {
        ConfidenceResult combined = ConfidenceService::combine(confidence_history);
        if (combined.score < RECOVERY_THRESHOLD) {
          ToolCall rc = select_recovery_tool(goal, type, evidence, result.tool_history);
          if (!rc.tool.empty()) {
            recovery_count++;
            evidence.add_fact("recovery:attempt=" + std::to_string(recovery_count));
            std::string rc_sig = rc.tool + ":" + rc.args;
            if (seen_tool_calls.insert(rc_sig).second) {
              ui.show_tool_invocation(rc.tool, rc.args);
              ToolResult rtr = run_tool(rc);
              rtr.tool = rc.tool;
              rtr.args = rc.args;
              result.tool_history.push_back(rtr);
              ui.show_tool_output(rtr.out);
              std::string rfact = rc.tool;
              if (!rc.args.empty()) rfact += " " + rc.args;
              evidence.add_fact(rfact);
              evidence.facts.push_back("[" + rfact + "] " + rtr.out.substr(0, 200));
              if (rtr.tool == "grep" && !rtr.out.empty() && rtr.out != "no matches") {
                evidence.add_fact("grep:results");
                evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                            rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
              } else if (rtr.tool == "read" && !rtr.out.empty() && rtr.out != "no files to read") {
                evidence.add_fact("read:results");
                evidence.add_evidence_entry(EvidenceClass::FileContent,
                                            rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
              } else if (rtr.tool == "discovery" && !rtr.out.empty()) {
                evidence.add_fact("discovery:results");
                evidence.add_evidence_entry(EvidenceClass::Discovery,
                                            rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
              } else if (rtr.tool == "find" && !rtr.out.empty() && rtr.out != "no matches") {
                evidence.add_fact("find:results");
                evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                            rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
              }
              // Evaluate confidence for recovery tool (normal category, not hardcoded 0.5)
              {
                ConfidenceResult cr;
                cr.target = rtr.args;
                if (rtr.tool == "grep") {
                  int hits = 0;
                  std::istringstream ss(rtr.out);
                  std::string line;
                  while (std::getline(ss, line))
                    if (!line.empty() && line != "no matches") hits++;
                  cr = ConfidenceService::after_search(rtr.args, hits);
                } else if (rtr.tool == "read") {
                  cr = ConfidenceService::after_read(1, true);
                } else if (rtr.tool == "references") {
                  bool ok = rtr.out != "no matches" && !rtr.out.empty();
                  cr.category = "search";
                  cr.score = ok ? 0.8 : 0.3;
                  cr.reason = ok ? "references matched callers" : "references found no callers";
                } else if (rtr.tool == "find") {
                  bool ok = rtr.out != "no matches" && !rtr.out.empty();
                  cr.category = "search";
                  cr.score = ok ? 0.8 : 0.3;
                  cr.reason = ok ? "find matched files" : "find found nothing";
                } else if (rtr.tool == "cmake") {
                  bool ok = rtr.out.find("error") == std::string::npos;
                  cr = ConfidenceService::after_build(ok, ok ? "" : rtr.out.substr(0, 200));
                } else if (rtr.tool == "ctest") {
                  cr.category = "verification";
                  bool ok = (rtr.out.find("failed") == std::string::npos &&
                             rtr.out.find("FAILED") == std::string::npos);
                  cr.score = ok ? 0.9 : 0.3;
                  cr.reason = ok ? "tests passed" : "tests failed";
                } else if (rtr.tool == "discovery") {
                  cr.category = "discovery";
                  bool ok = !rtr.out.empty();
                  cr.score = ok ? 0.70 : 0.30;
                  cr.reason = ok ? "project structure analysed" : "discovery failed";
                } else {
                  cr.category = "tool_generic";
                  cr.score = 0.5;
                  cr.reason = "recovery tool executed: " + rtr.tool;
                }
                confidence_history.push_back(cr);
              }
              // Re-check completion after recovery tool
              if (use_goal_routing
                  ? check_completion_goal(result.parsed_goal.goal, evidence)
                  : check_completion(goal, type, evidence)) {
                // Recovery produced evidence; loop will break with updated confidence
              }
            }
          }
        }
      }
      break;
    }

    ToolCall tc = select_next_tool(goal, type, evidence, result.tool_history);

    // When primary sequence is exhausted, try recovery
    if (tc.tool.empty() && recovery_count < MAX_RECOVERY) {
      tc = select_recovery_tool(goal, type, evidence, result.tool_history);
      if (!tc.tool.empty()) {
        recovery_count++;
        evidence.add_fact("recovery:attempt=" + std::to_string(recovery_count));
      }
    }

    if (tc.tool.empty())
      break;

    // Deduplicate: if the exact same tool+args was already executed
    std::string tc_signature = tc.tool + ":" + tc.args;
    if (!seen_tool_calls.insert(tc_signature).second) {
      // If recovery tool was a duplicate, try another recovery approach
      if (recovery_count < MAX_RECOVERY) {
        tc = select_recovery_tool(goal, type, evidence, result.tool_history);
        if (tc.tool.empty()) break;
        tc_signature = tc.tool + ":" + tc.args;
        if (!seen_tool_calls.insert(tc_signature).second) break;
        recovery_count++;
        evidence.add_fact("recovery:attempt=" + std::to_string(recovery_count));
      } else {
        result.stopped_early = true;
        result.stop_reason = "duplicate tool call: " + tc_signature;
        break;
      }
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
        evidence.add_fact("grep:noresults");
      }
    } else if (tc.tool == "read") {
      has_results = !tr.out.empty() && tr.out != "no files to read";
      result.recovery_metrics.read_attempts++;
      if (has_results) {
        result.recovery_metrics.read_success++;
      }
    } else if (tc.tool == "references") {
      has_results = !tr.out.empty() && tr.out != "no matches";
      if (has_results) {
        result.retrieval_metrics.reference_tool_hits++;
      } else {
        evidence.add_fact("references:noresults");
      }
      evidence.add_fact("references:done");
    } else if (tc.tool == "find") {
      has_results = !tr.out.empty() && tr.out != "no matches";
      result.recovery_metrics.find_attempts++;
      if (has_results) {
        result.recovery_metrics.find_success++;
        // Parse retrieval metrics from find tool output
        std::string winner_path;
        std::string winner_reason;
        std::vector<std::string> candidates;
        std::istringstream ss(tr.out);
        std::string line;
        while (std::getline(ss, line)) {
          if (line.compare(0, 10, "CANDIDATE:") == 0) {
            // "CANDIDATE: path score reason"
            auto rest = line.substr(10);
            // trim leading space
            if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
            auto first_space = rest.find(' ');
            auto second_space = rest.find(' ', first_space + 1);
            if (first_space != std::string::npos && second_space != std::string::npos) {
              std::string path = rest.substr(0, first_space);
              candidates.push_back(path);
            }
          } else if (line.compare(0, 9, "SELECTED:") == 0) {
            auto rest = line.substr(9);
            if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
            winner_path = rest;
          } else if (line.compare(0, 7, "REASON:") == 0) {
            auto rest = line.substr(7);
            if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
            winner_reason = rest;
          }
        }
        result.retrieval_metrics.trace_candidates = candidates;
        result.retrieval_metrics.selected_candidate = winner_path;
        result.retrieval_metrics.selection_reason = winner_reason;
        // Classify the hit type based on reason
        if (winner_reason.find("filename") != std::string::npos) {
          result.retrieval_metrics.filename_hits++;
        } else if (winner_reason.find("symbol") != std::string::npos) {
          result.retrieval_metrics.symbol_hits++;
        } else if (winner_reason.find("directory") != std::string::npos) {
          result.retrieval_metrics.directory_hits++;
        } else {
          result.retrieval_metrics.grep_hits++;
        }
      } else {
        result.retrieval_metrics.grep_hits++;
        evidence.add_fact("find:noresults");
      }
      evidence.add_fact("find:done");
    } else {
      has_results = !tr.out.empty();
    }
    if (has_results)
      evidence.add_fact(tc.tool + ":results");

    // Mark evidence class based on tool (only when results produced).
    // Use quality-aware entries: grep with many matches is Weak (imprecise),
    // few matches is Moderate. Find/references produce more precise matches.
    if (has_results) {
      if (tc.tool == "grep") {
        // Count matches to determine quality
        int match_lines = 0;
        std::istringstream gcount(tr.out);
        std::string gl;
        while (std::getline(gcount, gl))
          if (!gl.empty() && gl != "no matches") match_lines++;
        auto q = (match_lines <= 10) ? Moderate : Weak;
        evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                    tc.tool, tc.args, q, Medium, match_lines, 0, false);
      } else if (tc.tool == "references") {
        evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                    tc.tool, tc.args, Moderate, Medium, 0, 0, false);
      } else if (tc.tool == "read") {
        evidence.add_evidence_entry(EvidenceClass::FileContent,
                                    tc.tool, tc.args, Moderate, Medium, 0, 0, false);
      } else if (tc.tool == "discovery") {
        evidence.add_evidence_entry(EvidenceClass::Discovery,
                                    tc.tool, tc.args, Moderate, Medium, 0, 0, false);
      } else if (tc.tool == "cmake" && tr.out.find("error") == std::string::npos) {
        evidence.add_evidence_entry(EvidenceClass::Build,
                                    tc.tool, tc.args, Moderate, Medium, 0, 0, false);
      } else if (tc.tool == "ctest" &&
                 tr.out.find("failed") == std::string::npos &&
                 tr.out.find("FAILED") == std::string::npos) {
        evidence.add_evidence_entry(EvidenceClass::Test,
                                    tc.tool, tc.args, Moderate, Medium, 0, 0, false);
      } else if (tc.tool == "gh") {
        evidence.add_evidence_entry(EvidenceClass::CIWorkflow,
                                    tc.tool, tc.args, Moderate, Medium, 0, 0, false);
      } else if (tc.tool == "git") {
        evidence.add_evidence_entry(EvidenceClass::GitLog,
                                    tc.tool, tc.args, Moderate, High, 0, 0, false);
      } else if (tc.tool == "find") {
        // Find quality depends on match reason (filename/symbol = Strong)
        bool is_exact = (tr.out.find("REASON: filename match") != std::string::npos ||
                         tr.out.find("REASON: symbol match") != std::string::npos);
        auto q = is_exact ? Strong : Moderate;
        evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                    tc.tool, tc.args, q, Medium, 0, 0, false);
      }
    }

    // Evaluate confidence after each tool run
    ConfidenceResult cr;
    cr.target = tc.args;
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
      cr.target = tc.args.empty() ? last_search_target : tc.args;
      if (!tc.args.empty()) last_search_target = tc.args;
    } else if (tc.tool == "read") {
      cr = ConfidenceService::after_read(1, true);
      // Read tool often has empty args; use last search target for convergence
      cr.target = tc.args.empty() ? last_search_target : tc.args;
    } else if (tc.tool == "references") {
      cr.category = "search";
      cr.target = tc.args.empty() ? last_search_target : tc.args;
      if (!tc.args.empty()) last_search_target = tc.args;
      bool ok = tr.out != "no matches" && !tr.out.empty();
      cr.score = ok ? 0.8 : 0.3;
      cr.reason = ok ? "references matched callers" : "references found no callers";
    } else if (tc.tool == "find") {
      cr.category = "search";
      cr.target = tc.args.empty() ? last_search_target : tc.args;
      if (!tc.args.empty()) last_search_target = tc.args;
      bool ok = tr.out != "no matches" && !tr.out.empty();
      cr.score = ok ? 0.8 : 0.3;
      cr.reason = ok ? "find matched files" : "find found nothing";
    } else if (tc.tool == "cmake") {
      bool cmake_ok = tr.out.find("error") == std::string::npos;
      cr = ConfidenceService::after_build(cmake_ok, cmake_ok ? "" : tr.out.substr(0, 200));
      cr.target = tc.args;
    } else if (tc.tool == "ctest") {
      cr.category = "verification";
      cr.target = tc.args;
      bool ok = (tr.out.find("failed") == std::string::npos &&
                 tr.out.find("FAILED") == std::string::npos);
      cr.score = ok ? 0.9 : 0.3;
      cr.reason = ok ? "tests passed" : "tests failed";
    } else if (tc.tool == "git") {
      cr.category = "git";
      cr.target = tc.args;
      bool ok = !tr.out.empty() && tr.out.find("fatal") == std::string::npos;
      cr.score = ok ? 0.75 : 0.30;
      cr.reason = ok ? "git results" : "git found nothing";
    } else if (tc.tool == "gh") {
      cr.category = "ci";
      cr.target = tc.args;
      bool ok = !tr.out.empty();
      cr.score = ok ? 0.80 : 0.30;
      cr.reason = ok ? "CI data retrieved" : "CI data unavailable";
    } else if (tc.tool == "discovery") {
      cr.category = "discovery";
      cr.target = tc.args;
      bool ok = !tr.out.empty();
      cr.score = ok ? 0.70 : 0.30;
      cr.reason = ok ? "project structure analysed" : "discovery failed";
    } else {
      cr.category = "tool_generic";
      cr.target = tc.args;
      cr.score = 0.5;
      cr.reason = "tool executed";
    }

    confidence_history.push_back(cr);

    // Check if confidence is too low to continue
    ConfidenceResult combined = ConfidenceService::combine(confidence_history);
    if (ConfidenceService::should_stop(combined, 0.2)) {
      // Try recovery first
      if (recovery_count < MAX_RECOVERY) {
        ToolCall rc = select_recovery_tool(goal, type, evidence, result.tool_history);
        if (!rc.tool.empty()) {
          recovery_count++;
          evidence.add_fact("recovery:attempt=" + std::to_string(recovery_count));
          // Add the recovery tool directly without going through select_next_tool
          std::string rc_sig = rc.tool + ":" + rc.args;
          if (seen_tool_calls.insert(rc_sig).second) {
            ui.show_tool_invocation(rc.tool, rc.args);
            ToolResult rtr = run_tool(rc);
            rtr.tool = rc.tool;
            rtr.args = rc.args;
            result.tool_history.push_back(rtr);
            ui.show_tool_output(rtr.out);
            // Record evidence from recovery tool
            std::string rfact = rc.tool;
            if (!rc.args.empty()) rfact += " " + rc.args;
            evidence.add_fact(rfact);
            evidence.facts.push_back("[" + rfact + "] " + rtr.out.substr(0, 200));
            if (rtr.tool == "grep" && !rtr.out.empty() && rtr.out != "no matches") {
              evidence.add_fact("grep:results");
              evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                          rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
            } else if (rtr.tool == "read" && !rtr.out.empty() && rtr.out != "no files to read") {
              evidence.add_fact("read:results");
              evidence.add_evidence_entry(EvidenceClass::FileContent,
                                          rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
            } else if (rtr.tool == "discovery" && !rtr.out.empty()) {
              evidence.add_fact("discovery:results");
              evidence.add_evidence_entry(EvidenceClass::Discovery,
                                          rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
            } else if (rtr.tool == "find" && !rtr.out.empty() && rtr.out != "no matches") {
              evidence.add_fact("find:results");
              evidence.add_evidence_entry(EvidenceClass::FileSearch,
                                          rtr.tool, rtr.args, Moderate, Medium, 0, 0, false);
            }
            // Evaluate confidence for mid-loop recovery tool
            {
              ConfidenceResult rcr;
              rcr.target = rc.args.empty() ? last_search_target : rc.args;
              if (rc.tool == "grep") {
                int hits = 0;
                std::istringstream ss(rtr.out);
                std::string line;
                while (std::getline(ss, line))
                  if (!line.empty() && line != "no matches") hits++;
                rcr = ConfidenceService::after_search(rc.args, hits);
                rcr.target = rc.args.empty() ? last_search_target : rc.args;
                if (!rc.args.empty()) last_search_target = rc.args;
              } else if (rc.tool == "read") {
                rcr = ConfidenceService::after_read(1, true);
                rcr.target = rc.args.empty() ? last_search_target : rc.args;
              } else if (rc.tool == "references") {
                rcr.category = "search";
                bool ok = rtr.out != "no matches" && !rtr.out.empty();
                rcr.score = ok ? 0.8 : 0.3;
                rcr.reason = ok ? "references matched callers" : "references found no callers";
                rcr.target = rc.args.empty() ? last_search_target : rc.args;
                if (!rc.args.empty()) last_search_target = rc.args;
              } else if (rc.tool == "find") {
                rcr.category = "search";
                bool ok = rtr.out != "no matches" && !rtr.out.empty();
                rcr.score = ok ? 0.8 : 0.3;
                rcr.reason = ok ? "find matched files" : "find found nothing";
                rcr.target = rc.args.empty() ? last_search_target : rc.args;
                if (!rc.args.empty()) last_search_target = rc.args;
              } else if (rc.tool == "cmake") {
                bool cmake_ok = rtr.out.find("error") == std::string::npos;
                rcr = ConfidenceService::after_build(cmake_ok, cmake_ok ? "" : rtr.out.substr(0, 200));
                rcr.target = rc.args;
              } else if (rc.tool == "ctest") {
                rcr.category = "verification";
                bool ok = (rtr.out.find("failed") == std::string::npos &&
                           rtr.out.find("FAILED") == std::string::npos);
                rcr.score = ok ? 0.9 : 0.3;
                rcr.reason = ok ? "tests passed" : "tests failed";
                rcr.target = rc.args;
              } else if (rc.tool == "discovery") {
                rcr.category = "discovery";
                bool ok = !rtr.out.empty();
                rcr.score = ok ? 0.70 : 0.30;
                rcr.reason = ok ? "project structure analysed" : "discovery failed";
                rcr.target = rc.args;
              } else {
                rcr.category = "tool_generic";
                rcr.score = 0.5;
                rcr.reason = "recovery tool executed: " + rtr.tool;
                rcr.target = rc.args;
              }
              confidence_history.push_back(rcr);
            }
            // Re-evaluate completion after recovery
            if (use_goal_routing
                ? check_completion_goal(result.parsed_goal.goal, evidence)
                : check_completion(goal, type, evidence)) {
              result.stopped_early = false;
              result.stop_reason = "";
              continue;  // exit the loop normally at next iteration
            }
            // Recovery ran but completion not satisfied yet.
            // Don't break -- the next primary tool selection might complete it.
            // e.g., after find+grep (low confidence), recovery might add a search
            // tool, but we still need read. Let the primary sequence try read.
            continue;
          }
        }
      }
    }
  }

  // Show investigation summary
  if (!result.tool_history.empty()) {
    ui.show_investigation_complete();
  }

  // Build summary (planner debug log -- for inspect mode / ArchitectureReview)
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
  // Calibration breakdown
  for (const auto &bd : final_confidence.breakdown) {
    auto fmt_debug = [](double v) {
      std::ostringstream os;
      os.setf(std::ios::fixed);
      os.precision(2);
      os << v;
      return os.str();
    };
    summary += "  " + bd.category + ": max=" + fmt_debug(bd.max_score) +
               " agree=" + fmt_debug(bd.agreement) +
               " eff=" + fmt_debug(bd.effective_score) +
               " w=" + fmt_debug(bd.weight) +
               " c=" + fmt_debug(bd.contribution) + "\n";
  }

  // Build clean evidence summary for AI consumption (no planner metadata).
  // Extracts only the meaningful tool outputs as plain evidence statements.
  std::string evidence_summary;
  for (auto &tr : result.tool_history) {
    if (tr.out.empty() || tr.out == "no matches" || tr.out == "no files to read")
      continue;
    if (tr.tool == "git" || tr.tool == "gh") {
      // For git/gh, the full output is the answer (commit log, CI status, etc.)
      evidence_summary += tr.out;
      if (!evidence_summary.empty() && evidence_summary.back() != '\n')
        evidence_summary += '\n';
    } else if (tr.tool == "read") {
      // Extract file path from the "--- filename ---" header, then the content
      std::istringstream lines(tr.out);
      std::string line;
      std::string current_file;
      while (std::getline(lines, line)) {
        if (line.starts_with("--- ") && line.size() > 5) {
          current_file = line.substr(4, line.size() - 7);
          evidence_summary += "File: " + current_file + "\n";
          continue;
        }
        evidence_summary += "  " + line + "\n";
      }
    } else if (tr.tool == "find") {
      // Strip planner-internal CANDIDATE/SELECTED/REASON prefixes
      std::istringstream lines(tr.out);
      std::string line;
      while (std::getline(lines, line)) {
        if (line.starts_with("CANDIDATE:")) {
          auto rest = line.substr(10);
          // Format: " path score reason" → extract just "path"
          auto first_space = rest.find_first_not_of(' ');
          auto space_after_path = rest.find(' ', first_space + 1);
          if (first_space != std::string::npos && space_after_path != std::string::npos) {
            evidence_summary += "  " + rest.substr(first_space, space_after_path - first_space) + "\n";
          }
        } else if (line.starts_with("SELECTED:")) {
          auto rest = line.substr(9);
          auto pos = rest.find_first_not_of(' ');
          if (pos != std::string::npos)
            evidence_summary += "Selected: " + rest.substr(pos) + "\n";
        }
        // Skip "REASON:" lines -- not useful as evidence
      }
    } else if (tr.tool == "grep") {
      // Grep output is already clean: "file:line: content"
      evidence_summary += tr.out;
      if (!evidence_summary.empty() && evidence_summary.back() != '\n')
        evidence_summary += '\n';
    } else {
      // Any other tool output included as-is
      evidence_summary += tr.out;
      if (!evidence_summary.empty() && evidence_summary.back() != '\n')
        evidence_summary += '\n';
    }
  }

  // Compute caller_resolution_rate
  if (is_reference_query(goal)) {
    bool ran_grep = false;
    bool ran_references = false;
    for (const auto &tr : result.tool_history) {
      if (tr.tool == "grep") {
        ran_grep = true;
      }
      if (tr.tool == "references") {
        ran_references = true;
      }
    }
    bool resolved = use_goal_routing
        ? check_completion_goal(result.parsed_goal.goal, evidence)
        : check_completion(goal, type, evidence);
    result.retrieval_metrics.caller_resolution_rate = (resolved && ran_references && !ran_grep) ? 1.0 : 0.0;
  } else {
    result.retrieval_metrics.caller_resolution_rate = 0.0;
  }

  result.success = use_goal_routing
      ? check_completion_goal(result.parsed_goal.goal, evidence)
      : check_completion(goal, type, evidence);

  // Phase 4.1/4.2 shadow mode: run EvidenceGapEngine + Planner alongside
  // existing completion. Log agreement and metrics. Do not change behavior.
  if (use_goal_routing && result.parsed_goal.goal.is_known()) {
    EvidenceGapEngine gape;
    auto reqs = evidence_for_goal(result.parsed_goal.goal);
    auto gap = gape.evaluate(reqs, evidence);
    bool gap_complete = gap.complete();

    std::string verdict = (gap_complete == result.success) ? "agree" : "disagree";
    std::string gap_status = gap_complete ? "complete" : "incomplete";
    std::string cur_status = result.success ? "complete" : "incomplete";

    // Build per-requirement summary
    std::string detail;
    for (auto &rs : gap.requirements) {
      std::string st = rs.missing() ? "M" :
                       rs.weak() ? "W" :
                       rs.satisfied() ? "OK" : "?";
      detail += std::to_string(static_cast<int>(rs.requirement.ec)) + ":" + st +
                "/q" + std::to_string(static_cast<int>(rs.best_quality)) +
                "/rq" + std::to_string(static_cast<int>(rs.requirement.min_quality)) +
                "/v" + (rs.is_independently_verified ? "1" : "0") + " ";
    }

    // Gap metrics
    int m_cnt = gap.missing_count();
    int w_cnt = gap.weak_count();
    int u_cnt = gap.unverified_count();

    // Classify reason for disagreement
    std::string reason;
    if (verdict == "disagree") {
      if (gap_complete && !result.success) {
        reason = "gap_says_complete_but_current_does_not";
      } else if (!gap_complete && result.success) {
        reason = "current_says_complete_but_gap_finds=";
        if (m_cnt > 0) reason += std::to_string(m_cnt) + "missing_";
        if (w_cnt > 0) reason += std::to_string(w_cnt) + "weak_";
        if (u_cnt > 0) reason += std::to_string(u_cnt) + "unverified";
      }
    } else {
      if (w_cnt > 0 || u_cnt > 0)
        reason = "agrees_but_has_opportunities_weak=" + std::to_string(w_cnt) +
                 "_unverified=" + std::to_string(u_cnt);
      else
        reason = "identical";
    }

    // Phase 4.2 shadow: run Planner alongside (no behavioral change)
    Planner pln;
    auto decision = pln.decide(gap);

    evidence.add_fact("gap_shadow: " + verdict +
                      " current=" + cur_status +
                      " gap=" + gap_status +
                      " reason=" + reason +
                      " missing=" + std::to_string(m_cnt) +
                      " weak=" + std::to_string(w_cnt) +
                      " unverified=" + std::to_string(u_cnt) +
                      " planner=" + (decision.has_work ? decision.describe() : std::string("done")));
    evidence.add_fact("gap_shadow_detail: " + detail);
    std::cerr << "[GAP SHADOW] " << verdict
              << " m=" << m_cnt << " w=" << w_cnt << " u=" << u_cnt
              << " reason=" << reason
              << " planner=" << (decision.has_work ? decision.describe() : "done")
              << " detail=" << detail << std::endl;
  }

  if (type == ArchitectureReview) {
    summary = build_review_report(result.tool_history);
    evidence_summary = summary;
  }
  result.summary = summary;
  result.evidence_summary = evidence_summary;
  result.evidence = std::move(evidence);
  result.goal_type = static_cast<int>(type);
  result.confidence = final_confidence.score;

  // Recovery metrics (compute before outcome logic)
  result.recovery_metrics.attempts = iteration_count;
  result.recovery_metrics.evidence_found =
      result.evidence.has_fact_containing("grep:results") ||
      result.evidence.has_fact_containing("find:results") ||
      result.evidence.has_fact_containing("read:results") ||
      result.evidence.has_fact_containing("git:results") ||
      result.evidence.has_fact_containing("build") ||
      result.evidence.has_fact_containing("test") ||
      result.evidence.has_fact_containing("find:noresults") ||
      result.evidence.has_fact_containing("grep:noresults");
  result.recovery_metrics.verification_found =
      result.evidence.has_fact_containing("build") ||
      result.evidence.has_fact_containing("test");
  result.recovery_metrics.confidence_delta =
      final_confidence.score - (confidence_history.empty() ? 0.0
                                : confidence_history.front().score);

  // Compute strategy changes (number of times the tool type changes)
  std::string last_tool;
  for (const auto &tr : result.tool_history) {
    if (!last_tool.empty() && tr.tool != last_tool) {
      result.recovery_metrics.strategy_changes++;
    }
    last_tool = tr.tool;
  }

  // Detect if any tool call reverted files
  for (const auto &tr : result.tool_history) {
    if (tr.tool == "git" && (tr.args.find("revert") != std::string::npos ||
                             tr.args.find("checkout") != std::string::npos ||
                             tr.args.find("reset") != std::string::npos)) {
      result.trust_metrics.reverted = true;
      break;
    }
  }

  // Determine outcome
  if (result.stopped_early) {
    result.outcome = Core::Outcome::InsufficientEvidence;
  } else if (result.success) {
    result.outcome = Core::Outcome::Success;
  } else if (result.recovery_metrics.evidence_found) {
    // Evidence exists but is the wrong class -- judgment worked
    result.outcome = Core::Outcome::InsufficientEvidence;
  } else {
    result.outcome = Core::Outcome::Failure;
  }

  return result;
}

} // namespace Services

// ---------------------------------------------------------------------------
// InvestigationSession factory (defined outside Services namespace)
// ---------------------------------------------------------------------------

Core::InvestigationSession Core::InvestigationSession::from_result(
    const Services::ExecutionResult &result,
    std::string goal,
    std::chrono::milliseconds duration) {
  std::set<std::string> seen_files;

  Core::InvestigationSession session;
  session.goal = std::move(goal);
  session.conclusion = result.summary;
  session.confidence = result.confidence;
  session.duration = duration;
  session.outcome = result.outcome;
  session.sufficient_evidence = result.success;
  session.investigation_complete = !result.stopped_early;

  for (auto &tr : result.tool_history) {
    Core::ToolInvocation inv;
    inv.tool = tr.tool;
    inv.query = tr.args;

    if (tr.tool == "grep") {
      int matches = 0;
      std::istringstream s(tr.out);
      std::string l;
      while (std::getline(s, l))
        if (!l.empty() &&
            l.find("CANDIDATE:") == std::string::npos &&
            l.find("SELECTED:") == std::string::npos &&
            l.find("REASON:") == std::string::npos &&
            l.find("FILES:") == std::string::npos)
          matches++;
      inv.result = std::to_string(matches) + " match" + (matches == 1 ? "" : "es");
    } else if (tr.tool == "find") {
      inv.result = "files located";
    } else if (tr.tool == "read") {
      inv.result = "source read";
      std::istringstream s(tr.out);
      std::string l;
      while (std::getline(s, l)) {
        if (l.starts_with("--- ") && l.size() > 5) {
          std::string fname = l.substr(4, l.size() - 8);
          if (seen_files.insert(fname).second)
            session.files_examined.push_back(std::filesystem::path(fname));
        }
      }
    } else if (tr.tool == "git") {
      int commits = 0;
      std::istringstream s(tr.out);
      std::string l;
      while (std::getline(s, l))
        if (!l.empty()) commits++;
      inv.result = std::to_string(commits) + " commit" + (commits == 1 ? "" : "s");
    } else if (tr.tool == "gh") {
      inv.result = "CI data retrieved";
    } else if (tr.tool == "discovery") {
      inv.result = "project structure analysed";
    } else {
      inv.result = "executed";
    }
    session.tools_used.push_back(std::move(inv));
  }

  for (auto &fact : result.evidence.facts) {
    if (fact.size() > 200)
      session.evidence_summary.push_back(fact.substr(0, 200) + "...");
    else if (!fact.empty())
      session.evidence_summary.push_back(fact);
  }

  return session;
}
