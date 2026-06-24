#include "agent.h"
#include "core/metrics.h"
#include "services/execution_engine.h"
#include "services/file_service.h"
#include "ui/ui_manager.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string first_path(const std::string &grep_out) {
  if (grep_out.empty() || grep_out == "no matches") return {};
  auto colon = grep_out.find(':');
  if (colon == std::string::npos) return {};
  return grep_out.substr(0, colon);
}

static std::string evidence_keywords(const Services::EvidenceStore &ev) {
  std::string keys;
  for (auto &f : ev.facts) {
    if (f.empty()) continue;
    if (f[0] != '[') { // simple fact, not the detailed "[tool args] ..." line
      if (!keys.empty()) keys += "; ";
      keys += f;
    }
  }
  if (keys.empty()) {
    // fallback: show tool names from bracketed facts
    for (auto &f : ev.facts) {
      if (f.size() > 2 && f[0] == '[') {
        auto close = f.find(']');
        if (close != std::string::npos) {
          if (!keys.empty()) keys += "; ";
          keys += f.substr(1, close - 1);
        }
      }
    }
  }
  return keys.empty() ? "(none)" : keys;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

static const char *QUERIES[] = {
    // Calibration queries (23 known failure set)
    "grep Agent",
    "grep CommandRouter",
    "read file include/app/command_router.h",
    "show memory",
    "find xqkz_2024_quantum_entanglement module",
    "search for xqkz_2024_nonexistent_class in this codebase",
    "find the UIManager declaration",
    "where is DiscoveryService defined",
    "find the Outcome enum",
    "where is SessionState defined",
    "where is the dashboard",
    "search for benchmark service",
    "find the planning service",
    "where is the verification service",
    "find the version utility",
    "search for agent.cfg",
    "find the confidence heuristic",
    "search for evidence collector",
    "where is the discovery service",
    "find the benchmark results",
    "search for planning service fix",
    "how does the build system work",
    "find the build target",
    // Real-work stress queries
    "find CommandRouter",
    "find ExecutionEngine",
    "where is ReplayService used",
    "search for confidence scoring logic",
    "find why InsufficientEvidence is triggered",
    "grep SessionState usage",
    "find CI repair pipeline flow",
    "search benchmark failure #9",
    "find Agent responsibilities",
    "where does decision happen",
    // Investigation benchmarks
    "What is this repository?",
    "Explain this codebase.",
    "Where is the main executable defined?",
    "How does confidence scoring work?",
    // Negative test (expects evidence failure)
    "where is xqkz_2024_kubernetes_deployment",
    // Specific routing/extraction tests
    "how is evidence gating implemented",
    "where do we call gh run view",
    "tell me how repository investigation works",
    // Commit history routing tests
    "what is the last commit",
    "can you check the last commit",
    "show me the latest commit",
    // GitHub Actions benchmarks
    "can you check this log https://github.com/owner/repo/actions/runs/12345",
    "Why did CI fail?",
    "Which test failed?",
};
static constexpr int N = sizeof(QUERIES) / sizeof(QUERIES[0]);

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
  std::cout << std::left;
  std::cout
    << "Query|Outcome|Confidence|KeywordsInEvidence|Attempts" << "\n"
    << "-----|-------|----------|-----------------|--------" << "\n";

  for (int i = 0; i < N; ++i) {
    std::string q = QUERIES[i];
    std::string outcome_str = "error";
    double confidence = 0.0;
    std::string evidence_keys;
    int attempts = 0;

    try {
      Core::Agent agent;
      Core::UIManager ui(agent);
      Services::ExecutionEngine engine;

      std::string last_grep;
      auto runner = [&last_grep](const Services::ToolCall &tc) -> Services::ToolResult {
        Services::ToolResult tr;
        if (tc.tool == "grep") {
          auto results =
              Services::FileService::search_in_directory(".", tc.args, "*");
          if (results.empty()) {
            tr.stdout = "no matches";
          } else {
            std::ostringstream out;
            for (auto &r : results) {
              out << r.file_path << ":" << r.line_number << ": "
                  << r.line_content << "\n";
              if (out.tellp() > 10000) {
                out << "...";
                break;
              }
            }
            last_grep = out.str();
            tr.stdout = last_grep;
          }
          return tr;
        }
        if (tc.tool == "read") {
          std::string path = tc.args.empty() ? first_path(last_grep) : tc.args;
          if (path.empty()) {
            tr.stdout = "no file to read";
          } else {
            tr.stdout = Services::FileService::read_file_range(path, 0, 30);
          }
          return tr;
        }
        if (tc.tool == "gh") { tr.stdout = "[]"; return tr; }
        if (tc.tool == "cmake") { tr.stdout = "build succeeded"; return tr; }
        if (tc.tool == "ctest") { tr.stdout = "tests passed"; return tr; }
        if (tc.tool == "discovery") {
          tr.stdout = "Project type: C++ (cmake), sources: 42, services: 12, "
                 "has_tests: yes";
          return tr;
        }
        if (tc.tool == "git") {
          tr.stdout = "abc1234 feat: add evidence system\n"
                 "def5678 fix: improve read tool";
          return tr;
        }
        return tr;
      };

      auto result = engine.execute(q, runner, ui);
      outcome_str = Core::outcome_name(result.outcome);
      confidence = result.confidence;
      attempts = result.recovery_metrics.attempts;
      evidence_keys = evidence_keywords(result.evidence);
    } catch (std::exception &ex) {
      outcome_str = std::string("exception: ") + ex.what();
    } catch (...) {
      outcome_str = "unknown exception";
    }

    std::cout << q << "|" << outcome_str << "|" << std::fixed
              << std::setprecision(3) << confidence << "|" << evidence_keys
              << "|" << attempts << "\n";
  }

  return 0;
}
