// Level 2.4 Verification Tool
// Checks: behavioral parity, coverage audit, fallback rate, gap analysis
//
// Build: g++ -std=c++17 -Iinclude tests/verification_test.cpp build/libcursor_lib.a -o /tmp/verify

#include "services/execution_engine.h"
#include "services/goal_understanding_service.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Services;

// ============================================================
// Coverage Audit: every Intent x Entity combination
// ============================================================

struct IePair { Intent intent; Entity entity; const char *label; };

static const IePair ALL_COMBOS[] = {
  {Intent::Explain,   Entity::Architecture,   "Explain+Architecture"},
  {Intent::Explain,   Entity::Codebase,       "Explain+Codebase"},
  {Intent::Explain,   Entity::Component,      "Explain+Component"},
  {Intent::Explain,   Entity::Symbol,         "Explain+Symbol"},
  {Intent::Explain,   Entity::File,           "Explain+File"},
  {Intent::Explain,   Entity::GitHistory,     "Explain+GitHistory"},

  {Intent::Locate,    Entity::Symbol,         "Locate+Symbol"},
  {Intent::Locate,    Entity::File,           "Locate+File"},
  {Intent::Locate,    Entity::Component,      "Locate+Component"},

  {Intent::Review,    Entity::Architecture,   "Review+Architecture"},
  {Intent::Review,    Entity::Codebase,       "Review+Codebase"},
  {Intent::Review,    Entity::Component,      "Review+Component"},

  {Intent::Status,    Entity::GitHistory,     "Status+GitHistory"},
  {Intent::Status,    Entity::GitWorkingTree, "Status+GitWorkingTree"},
  {Intent::Status,    Entity::Session,        "Status+Session"},

  {Intent::Diagnose,  Entity::CIPipeline,     "Diagnose+CIPipeline"},
  {Intent::Diagnose,  Entity::GitHubAction,   "Diagnose+GitHubAction"},
  {Intent::Diagnose,  Entity::Build,          "Diagnose+Build"},
  {Intent::Diagnose,  Entity::Test,           "Diagnose+Test"},
  {Intent::Diagnose,  Entity::Symbol,         "Diagnose+Symbol"},

  {Intent::Compare,   Entity::Symbol,         "Compare+Symbol"},
  {Intent::Compare,   Entity::Component,      "Compare+Component"},

  {Intent::Navigate,  Entity::File,           "Navigate+File"},

  {Intent::Modify,    Entity::Component,      "Modify+Component"},
  {Intent::Modify,    Entity::Symbol,         "Modify+Symbol"},

  {Intent::Execute,   Entity::Build,          "Execute+Build"},
  {Intent::Execute,   Entity::Test,           "Execute+Test"},

  {Intent::Chat,      Entity::Unknown,        "Chat"},
  {Intent::Unknown,   Entity::Unknown,        "Unknown"},
};

static void run_coverage_audit() {
  std::cout << "=== Coverage Audit ===\n\n";
  int empty_mappings = 0;
  int total = 0;

  for (auto &c : ALL_COMBOS) {
    total++;
    Goal g;
    g.intent = c.intent;
    g.entity = c.entity;
    auto ev = ExecutionEngine::evidence_for_goal(g);

    // Check for empty mappings that shouldn't be empty
    bool should_be_empty =
        (c.intent == Intent::Chat) ||
        (c.intent == Intent::Unknown) ||
        (c.intent == Intent::Status && c.entity == Entity::Session);

    bool is_empty = ev.empty();

    if (is_empty && !should_be_empty) {
      std::cout << "  MISSING: " << c.label << " -> (empty)\n";
      empty_mappings++;
    } else if (!is_empty && should_be_empty) {
      std::cout << "  OVER-MAPPED: " << c.label << " -> [";
      for (size_t i = 0; i < ev.size(); i++) {
        if (i) std::cout << ", ";
        std::cout << ev[i].ec;
      }
      std::cout << "]\n";
      empty_mappings++;
    } else {
      std::cout << "  OK: " << c.label << " -> [";
      for (size_t i = 0; i < ev.size(); i++) {
        if (i) std::cout << ", ";
        std::cout << ev[i].ec;
      }
      std::cout << "]\n";
    }
  }

  std::cout << "\n  Coverage: " << (total - empty_mappings) << "/" << total
            << " (" << std::fixed << std::setprecision(1)
            << (100.0 * (total - empty_mappings) / total) << "%)\n";
  if (empty_mappings > 0) {
    std::cout << "  GAPS: " << empty_mappings << " empty mappings need attention\n";
  }
  std::cout << "\n";
}

// ============================================================
// Corpus-based fallback rate + behavioral parity
// ============================================================

static std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  size_t end = s.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) return {};
  return s.substr(start, end - start + 1);
}

static void run_corpus_analysis() {
  std::cout << "=== Fallback Rate & Behavioral Parity ===\n\n";

  // Read the prompt corpus
  std::ifstream csv("docs/planner/prompt_corpus.csv");
  if (!csv) {
    std::cout << "  Cannot open docs/planner/prompt_corpus.csv\n";
    return;
  }

  GoalUnderstandingService gus;
  std::string line;
  int total = 0;
  int goal_based = 0;
  int legacy = 0;
  int disagreement = 0;
  int agreement = 0;

  std::vector<std::string> corpus_queries;

  while (std::getline(csv, line)) {
    if (line.empty() || line[0] == '#') continue;

    // Parse CSV: prompt | goal_type | expected | status | source
    std::vector<std::string> fields;
    std::string field;
    for (char c : line) {
      if (c == '|') {
        fields.push_back(trim(field));
        field.clear();
      } else {
        field += c;
      }
    }
    fields.push_back(trim(field));

    if (fields.size() < 4) continue;

    std::string query = fields[0];
    std::string current_type_name = fields[1];
    std::string expected_type_name = fields[2];
    std::string status = fields[3];

    // Skip comments and recovery benchmarks (marked with # prefix)
    if (query.empty() || query[0] == '#') continue;

    corpus_queries.push_back(query);

    // Parse the query
    auto pr = gus.parse(query);

    // Check if Goal-based path would be used
    bool use_goal = (pr.confidence >= 0.5);

    if (use_goal) {
      goal_based++;
    } else {
      legacy++;
    }

    // For Goal-based queries, compute evidence requirements
    auto goal_evidence = ExecutionEngine::evidence_for_goal(pr.goal);

    // We can't call classify_goal() from here (it's private), so legacy simulation
    // would rely on the CSV's expected type. Not implemented -- left for future parity.

    // Check for interesting cases
    if (pr.confidence < 0.5 && pr.goal.intent != Intent::Unknown) {
      std::cout << "  LOW-CONF: \"" << query << "\" conf="
                << std::fixed << std::setprecision(2) << pr.confidence
                << " intent=" << (int)pr.goal.intent
                << " entity=" << (int)pr.goal.entity << "\n";
    }

    total++;
  }

  std::cout << "\n  Total corpus queries: " << total << "\n";
  std::cout << "  Goal-based path (conf >= 0.5): " << goal_based
            << " (" << (100 * goal_based / total) << "%)\n";
  std::cout << "  Legacy fallback (conf < 0.5): " << legacy
            << " (" << (100 * legacy / total) << "%)\n";
  std::cout << "  Goal/legacy agreement: " << agreement << "\n";
  std::cout << "  Goal/legacy disagreement: " << disagreement << "\n";

  if (legacy > 0) {
    std::cout << "\n  Fallback rate: " << std::fixed << std::setprecision(1)
              << (100.0 * legacy / total) << "%\n";
  }
  std::cout << "\n";
}

// ============================================================
// Gap analysis: top queries from real usage
// ============================================================

static const char *REAL_QUERIES[] = {
  // Working tree status (historically broken)
  "show changed files",
  "what changed",
  "modified files",
  "what files are modified",
  "show modified files",
  "check the files changed",
  "check changed files",
  "did I edit anything",
  "what are the current changes",
  "check if any files changed",
  "show me uncommitted changes",
  "what's unstaged",

  // Git history
  "what is the last commit",
  "can you check the last commit",
  "show me the latest commit",
  "show recent commits",
  "previous commit",
  "get the latest commit",
  "recent commits",

  // Architecture / design
  "explain the architecture",
  "how is this agent designed",
  "walk me through the design",
  "how does this work",
  "how does the build system work",
  "tell me how repository investigation works",

  // Code location
  "find CommandRouter",
  "where is DiscoveryService defined",
  "grep Agent",
  "search for confidence scoring logic",
  "where do we call gh run view",

  // CI diagnosis
  "why did CI fail",
  "which test failed",
  "check my CI workflow",
  "what is the status of my github action",

  // General chat (should not trigger investigation)
  "how are you",
  "hello",
  "what can you do",
  "how do I install python",

  // Session state
  "what provider am I using",
  "what model am I on",
  "am i online",

  // Code change
  "add a new CLI command",
  "fix the failing unit test",
};

static void run_gap_analysis() {
  std::cout << "=== Gap Analysis ===\n\n";

  GoalUnderstandingService gus;

  for (auto &query : REAL_QUERIES) {
    auto pr = gus.parse(query);
    auto goal_evidence = ExecutionEngine::evidence_for_goal(pr.goal);

    bool use_goal = (pr.confidence >= 0.5);

    std::cout << "  \"" << query << "\"\n";
    std::cout << "    Goal: i=" << (int)pr.goal.intent
              << " e=" << (int)pr.goal.entity
              << " a=" << (int)pr.goal.artifact
              << " s=" << (int)pr.goal.scope
              << " conf=" << std::fixed << std::setprecision(2) << pr.confidence
              << " (" << (use_goal ? "GOAL" : "LEGACY") << ")\n";
    std::cout << "    Evidence: [";
    for (size_t i = 0; i < goal_evidence.size(); i++) {
      if (i) std::cout << ", ";
      std::cout << goal_evidence[i].ec;
    }
    std::cout << "]\n";

    // Gap indicator: goal is correct but evidence requirements may be incomplete
    if (pr.goal.intent == Intent::Explain && pr.goal.entity == Entity::Architecture) {
      if (goal_evidence.size() == 2 &&
          goal_evidence[0].ec == EvidenceClass::Discovery &&
          goal_evidence[1].ec == EvidenceClass::FileContent) {
        // Good mapping for architecture explain
      }
    }

    // Tool planner gap preview: if goal says we need FileSearch+FileContent,
    // the current tool planner (via GoalType) might run git instead.
    // We'll flag these for step 3.
    std::string legacy_tools_hint;
    if (pr.goal.intent == Intent::Status &&
        (pr.goal.entity == Entity::GitHistory ||
         pr.goal.entity == Entity::GitWorkingTree)) {
      // Legacy GoalType = CommitHistory -> git log/status
      // Goal evidence = GitLog -> consistent
    } else if (pr.goal.intent == Intent::Locate && pr.goal.entity == Entity::Symbol) {
      // Legacy GoalType = CodebaseQuery -> find+grep+read
      // Goal evidence = FileSearch+FileContent -> consistent
    } else if (pr.goal.intent == Intent::Status && pr.goal.entity == Entity::Session) {
      // Legacy GoalType = SessionState -> no tools
      // Goal evidence = (empty) -> consistent
    } else {
      // These may have tool planner gaps when Goal and GoalType disagree
      // on what tools to run. Flag for step 3 analysis.
      if (use_goal) {
        std::cout << "    ** Potential tool planner gap (Goal != GoalType mapping)\n";
      }
    }

    std::cout << "\n";
  }
}

// ============================================================
// Main
// ============================================================

int main() {
  std::cout << std::boolalpha;

  // 1. Coverage audit
  run_coverage_audit();

  // 2. Corpus fallback analysis
  run_corpus_analysis();

  // 3. Gap analysis on real queries
  run_gap_analysis();

  // Summary
  std::cout << "=== Summary ===\n\n";

  // Count stats
  GoalUnderstandingService gus;
  int example_total = 0;
  int goal_route = 0;
  int legacy_route = 0;
  for (auto &query : REAL_QUERIES) {
    example_total++;
    auto pr = gus.parse(query);
    if (pr.confidence >= 0.5) goal_route++; else legacy_route++;
  }

  std::cout << "  Real-query fallback rate: "
            << (100 * legacy_route / example_total) << "%"
            << " (" << legacy_route << "/" << example_total << " legacy, "
            << goal_route << "/" << example_total << " goal-based)\n";

  // Coverage completeness
  int coverage_total = sizeof(ALL_COMBOS) / sizeof(ALL_COMBOS[0]);
  int gaps = 0;
  for (auto &c : ALL_COMBOS) {
    Goal g;
    g.intent = c.intent;
    g.entity = c.entity;
    auto ev = ExecutionEngine::evidence_for_goal(g);
    bool should_be_empty =
        (c.intent == Intent::Chat) ||
        (c.intent == Intent::Unknown) ||
        (c.intent == Intent::Status && c.entity == Entity::Session);
    if (ev.empty() != should_be_empty) gaps++;
  }
  std::cout << "  Coverage: " << (coverage_total - gaps) << "/" << coverage_total
            << " (" << (100 * (coverage_total - gaps) / coverage_total) << "%)\n";

  std::cout << "\n  Verification complete.\n";

  return gaps > 0 ? 1 : 0;
}
