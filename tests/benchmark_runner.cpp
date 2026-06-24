#include "services/execution_engine.h"
#include "services/file_service.h"
#include "services/discovery_service.h"
#include "services/command_service.h"
#include "ui/ui_manager.h"
#include "agent.h"
#include "core/metrics.h"

#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>

using json = nlohmann::json;

struct BenchmarkResult {
  std::string query;
  std::string actual_goal;
  std::string expected_goal;
  std::vector<std::string> tools;
  int iterations;
  std::string outcome;
  std::string expected_outcome;
  bool passed;
};

static std::string first_path(const std::string &grep_out) {
  if (grep_out.empty() || grep_out == "no matches") return {};
  auto colon = grep_out.find(':');
  if (colon == std::string::npos) return {};
  return grep_out.substr(0, colon);
}

int main(int argc, char **argv) {
  std::string suite_path = "scenarios/benchmark/benchmark_suite.json";
  if (argc > 1) suite_path = argv[1];

  std::ifstream ifs(suite_path);
  if (!ifs) {
    std::cerr << "Cannot open " << suite_path << "\n";
    return 1;
  }
  json suite;
  ifs >> suite;

  auto queries = suite["queries"];
  std::vector<BenchmarkResult> results;

  std::string last_grep;
  int total = 0, passed = 0;

  for (auto &q : queries) {
    std::string query = q["query"];
    std::string expected_goal = q.value("goal_type", "?");
    std::string expected_outcome = q.value("outcome", "?");
    total++;

    std::cout << "[" << total << "/" << queries.size() << "] "
              << query << "\n";

    try {
      Core::Agent agent;
      Core::UIManager ui(agent);
      Services::ExecutionEngine engine;

      auto runner = [&last_grep, &query](const Services::ToolCall &tc) -> Services::ToolResult {
        Services::ToolResult tr;
        if (tc.tool == "grep") {
          auto results = Services::FileService::search_in_directory(
              ".", tc.args.empty() ? query : tc.args, "*");
          results.erase(std::remove_if(results.begin(), results.end(),
              [](const auto &r) {
                return r.file_path.find("benchmark_suite.json") != std::string::npos
                    || r.file_path.find("tests/") != std::string::npos
                    || r.file_path.find("scenarios/") != std::string::npos;
              }), results.end());
          if (results.empty()) {
            tr.out = "no matches";
          } else {
            std::ostringstream out;
            for (auto &r : results) {
              out << r.file_path << ":" << r.line_number << ": "
                  << r.line_content << "\n";
              if (out.tellp() > 10000) { out << "..."; break; }
            }
            last_grep = out.str();
            tr.out = last_grep;
          }
          return tr;
        }
        if (tc.tool == "read") {
          std::string path = tc.args.empty() ? first_path(last_grep) : tc.args;
          if (path.empty()) { tr.out = "no file to read"; return tr; }
          tr.out = Services::FileService::read_file_range(path, 0, 30);
          return tr;
        }
        if (tc.tool == "gh") { tr.out = "[]"; return tr; }
        if (tc.tool == "git") {
          tr.out = Services::CommandService::execute("git " + tc.args);
          return tr;
        }
        if (tc.tool == "cmake") { tr.out = "build succeeded"; return tr; }
        if (tc.tool == "ctest") { tr.out = "tests passed"; return tr; }
        if (tc.tool == "discovery") {
          auto d = Services::DiscoveryService::scan(".", query);
          tr.out = "Project: " + d.project_type + "\n";
          tr.out += "Sources: " + std::to_string(d.source_file_count) + "\n";
          tr.out += "Tests: " + std::string(d.has_tests ? "yes" : "no") + "\n";
          return tr;
        }
        tr.out = "unknown tool";
        return tr;
      };

      auto result = engine.execute(query, runner, ui);
      std::vector<std::string> tools;
      for (auto &tr : result.tool_history)
        tools.push_back(tr.tool + " " + tr.args);

      std::string actual_goal = Services::ExecutionEngine::goal_type_name(
          static_cast<Services::ExecutionEngine::GoalType>(result.goal_type));

      std::string actual_outcome = Core::outcome_name(result.outcome);
      std::string expected_norm;
      for (size_t i = 0; i < expected_outcome.size(); i++) {
        if (expected_outcome[i] >= 'A' && expected_outcome[i] <= 'Z') {
          if (!expected_norm.empty()) expected_norm += '_';
          expected_norm += static_cast<char>(expected_outcome[i] + 32);
        } else {
          expected_norm += expected_outcome[i];
        }
      }
      bool ok = (actual_outcome == expected_norm);

      results.push_back({
        query, actual_goal, expected_goal,
        tools, (int)result.tool_history.size(),
        actual_outcome, expected_outcome, ok
      });
      if (ok) passed++;

      std::cout << "  Goal: " << actual_goal
                << "  Outcome: " << actual_outcome
                << (ok ? " ✓" : " ✗ expected " + expected_outcome)
                << "  Tools: " << tools.size()
                << "  Iters: " << result.recovery_metrics.attempts << "\n";

    } catch (std::exception &e) {
      std::cout << "  Error: " << e.what() << "\n";
      results.push_back({query, "error", expected_goal, {}, 0, "error", expected_outcome, false});
    }
  }

  // Summary
  std::cout << "\n========================================\n";
  std::cout << "  Benchmark Summary\n";
  std::cout << "========================================\n";
  std::cout << "  Total: " << total << "\n";
  std::cout << "  Passed: " << passed << "\n";
  std::cout << "  Failed: " << (total - passed) << "\n";
  std::cout << "  Success rate: " << std::fixed << std::setprecision(1)
            << (100.0 * passed / total) << "%\n";

  double avg_tools = 0;
  int avg_iters = 0;
  for (auto &r : results) {
    avg_tools += r.tools.size();
    avg_iters += r.iterations;
  }
  std::cout << "  Avg tools/query: " << std::setprecision(2)
            << (avg_tools / total) << "\n";
  std::cout << "  Avg iterations: " << std::setprecision(2)
            << (avg_iters / total) << "\n";

  // Failure breakdown
  std::map<std::string, int> outcome_counts;
  for (auto &r : results)
    outcome_counts[r.outcome]++;
  std::cout << "\n  Outcomes:\n";
  for (auto &[oc, cnt] : outcome_counts)
    std::cout << "    " << oc << ": " << cnt << "\n";

  // List failures
  bool header_printed = false;
  for (auto &r : results) {
    if (r.passed) continue;
    if (!header_printed) {
      std::cout << "\n  Failures:\n";
      header_printed = true;
    }
    std::cout << "    " << r.query << "\n";
    std::cout << "      Expected: " << r.expected_outcome
              << "  Got: " << r.outcome << "\n";
    std::cout << "      Goal: " << r.actual_goal
              << "  Tools: ";
    for (auto &t : r.tools) std::cout << t << "; ";
    std::cout << "\n";
  }

  return passed == total ? 0 : 1;
}
