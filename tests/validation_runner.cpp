#include "services/execution_engine.h"
#include "services/file_service.h"
#include "services/find_service.h"
#include "services/discovery_service.h"
#include "services/command_service.h"
#include "ui/ui_manager.h"
#include "agent.h"
#include "core/metrics.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>

using namespace std::chrono;

static std::string first_path(const std::string &grep_out) {
  if (grep_out.empty() || grep_out == "no matches") return {};
  auto colon = grep_out.find(':');
  if (colon == std::string::npos) return {};
  return grep_out.substr(0, colon);
}

static std::string classify_failure(const std::string &outcome,
                                     const std::string &,
                                     const std::string &,
                                     const std::vector<std::string> &tools) {
  if (outcome != "success") {
    if (tools.empty() || (tools.size() == 1 && tools[0].find("grep") != std::string::npos &&
                          outcome == "insufficient_evidence"))
      return "Retrieval";
    if (tools.size() >= 3 && outcome == "insufficient_evidence")
      return "Gate";
    return "Retrieval";
  }
  return "None";
}

static std::string root_cause(const std::string &,
                                const std::string &,
                                const std::string &,
                                const std::vector<std::string> &tools,
                                const std::string &failure_class) {
  if (failure_class == "Retrieval") {
    if (tools.empty()) return "No tools executed — classified as GeneralChat instead of investigation";
    if (tools.size() == 1 && tools[0].find("grep") != std::string::npos)
      return "Grep returned no matches for extracted search term";
    return "Evidence insufficient after " + std::to_string(tools.size()) + " tools";
  }
  if (failure_class == "Gate")
    return "Completion gate triggered before sufficient evidence collected";
  return "No failure detected";
}

static std::string repair_candidate(const std::string &failure_class) {
  if (failure_class == "Routing") return "ReclassifyGoal";
  if (failure_class == "Ranking") return "ExpandTopK";
  if (failure_class == "Gate")    return "ContinueInvestigation";
  if (failure_class == "Retrieval") return "ExpandSearchTerms";
  return "None";
}

static bool is_recoverable(const std::string &failure_class) {
  return failure_class == "Routing" || failure_class == "Ranking" ||
         failure_class == "Gate" || failure_class == "Retrieval";
}

struct ValidationEntry {
  std::string query;
  std::string goal_type;
  std::string outcome;
  int iterations;
  std::vector<std::string> tools;
  int files_read;
  double duration_ms;
  std::string failure_class;
  bool recoverable;
  std::string root_cause_str;
  std::string repair;
  int duplicate_tools;
  int failed_tools;
};

int main(int, char **) {
  std::vector<std::string> queries = {
    // Repository Investigation
    "where is replay implemented",
    "where is TraceConsumer used",
    "find checkpoint service",
    "where do we call gh run view",
    // Architecture
    "tell me how repository investigation works",
    "explain the telemetry pipeline",
    "how is \"evidence\" gating implemented",
    // Git
    "what is the last commit",
    "show current git status",
    "what files changed in the last commit",
    "show recent commits",
    "what branch am I on",
    // Session State
    "what provider am I using",
    "what model am I using",
    "am I online",
    "which backend is active",
    "which provider is selected",
    // Architecture Review
    "review architecture",
    "review codebase",
    "review recent changes",
    // General Chat
    "hello there",
    "how are you doing",
    // Code Change
    "add a new field to \"session_state\"",
    "fix compile warnings in \"auth_service\"",
    // CI Check
    "check the ci build status",
    "did the last workflow pass",
    // GitHub Investigation
    "check run https://github.com/bniladridas/cursor/actions/runs/28139237680",
    "investigate job https://github.com/bniladridas/cursor/actions/runs/28139237680/job/83332734648"
  };

  std::cout << "Cursor Validation Protocol\n";
  std::cout << "==========================\n\n";

  std::vector<ValidationEntry> entries;
  std::string last_grep;
  int passed = 0;

  for (size_t qi = 0; qi < queries.size(); qi++) {
    auto &query = queries[qi];
    std::cout << "--- Query " << (qi + 1) << " ---\n";
    std::cout << query << "\n\n";

    try {
      Core::Agent agent;
      Core::UIManager ui(agent);
      Services::ExecutionEngine engine;

      std::string last_find;
      auto runner = [&last_grep, &last_find, &query](const Services::ToolCall &tc) -> Services::ToolResult {
        Services::ToolResult tr;
        if (tc.tool == "grep") {
          std::string pattern = tc.args;
          if (pattern.empty()) {
            size_t first_quote = query.find('"');
            size_t last_quote = query.rfind('"');
            if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
              pattern = query.substr(first_quote + 1, last_quote - first_quote - 1);
            } else {
              pattern = "session_state";
            }
          }
          auto results = Services::FileService::search_in_directory(
              ".", pattern, "*");
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
        if (tc.tool == "find") {
          std::string term = tc.args;
          auto impl_pos = term.find(" --impl");
          bool impl_query = (impl_pos != std::string::npos);
          if (impl_query) term = term.substr(0, impl_pos);
          auto candidates = Services::directory_aware_find(term, impl_query);
          if (candidates.empty()) { tr.out = "no matches"; return tr; }
          last_find = candidates[0].path;
          for (auto &c : candidates)
            tr.out += "CANDIDATE: " + c.path + " " + std::to_string(c.score) + " " + c.reason + "\n";
          tr.out += "SELECTED: " + candidates[0].path + "\n";
          tr.out += "REASON: " + candidates[0].reason + "\n";
          tr.out += "FILES:\n";
          for (auto &c : candidates)
            tr.out += c.path + "\n";
          return tr;
        }
        if (tc.tool == "read") {
          std::vector<std::string> files;
          if (!tc.args.empty()) {
            std::istringstream ss(tc.args);
            std::string f;
            while (ss >> f) files.push_back(f);
          } else if (!last_find.empty()) {
            files.push_back(last_find);
          } else if (!last_grep.empty()) {
            files.push_back(first_path(last_grep));
          }
          if (files.empty()) { tr.out = "no files to read"; return tr; }
          tr.out.clear();
          for (auto &f : files) {
            std::string content = Services::FileService::read_file_range(f, 1, 30);
            if (!content.empty()) {
              tr.out += "--- " + f + " ---\n" + content + "\n";
            }
          }
          if (tr.out.empty()) tr.out = "no files to read";
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

      auto start = high_resolution_clock::now();
      auto result = engine.execute(query, runner, ui);
      auto end = high_resolution_clock::now();

      double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
      std::string gtype = Services::ExecutionEngine::goal_type_name(
          static_cast<Services::ExecutionEngine::GoalType>(result.goal_type));
      std::string otype = Core::outcome_name(result.outcome);

      std::vector<std::string> tools;
      for (auto &tr : result.tool_history)
        tools.push_back(tr.tool + " " + tr.args);

      int duplicate_tools = 0;
      {
        std::set<std::string> seen;
        for (auto &tr : result.tool_history) {
          std::string key = tr.tool + "|" + tr.args;
          if (!seen.insert(key).second) duplicate_tools++;
        }
      }

      int failed_tools = 0;
      for (auto &tr : result.tool_history)
        if (!tr.success()) failed_tools++;

      int files = 0;
      for (auto &f : result.evidence.facts)
        if (f.find(":") != std::string::npos) files++;

      std::string fc = classify_failure(otype, query, gtype, tools);
      if (otype == "success") fc = "None";

      bool success = (otype == "success");
      if (success) passed++;

      entries.push_back({
        query, gtype, otype,
        result.recovery_metrics.attempts,
        tools, files, ms,
        fc, is_recoverable(fc),
        root_cause(otype, query, gtype, tools, fc),
        repair_candidate(fc),
        duplicate_tools, failed_tools
      });

      // Print trace
      std::cout << "  GoalType:     " << gtype << "\n";
      std::cout << "  Outcome:      " << otype << (success ? " ✓" : "") << "\n";
      std::cout << "  Iterations:   " << result.recovery_metrics.attempts << "\n";
      std::cout << "  Tools:        ";
      for (auto &t : tools) std::cout << t << "; ";
      std::cout << "\n";
      std::cout << "  Files Read:   " << files << "\n";
      std::cout << "  Duration:     " << std::fixed << std::setprecision(1) << ms << "ms\n";
      std::cout << "  Dup Tools:    " << duplicate_tools << "\n";
      std::cout << "  Fail Tools:   " << failed_tools << "\n";

    } catch (std::exception &e) {
      std::cout << "  Error: " << e.what() << "\n";
      entries.push_back({query, "error", "error", 0, {}, 0, 0, "Routing", true, "Exception during execution", "ReclassifyGoal", 0, 0});
    }
    std::cout << "\n";
  }

  // === SUCCESS REPORT ===
  std::cout << "========================================\n";
  std::cout << "  SUCCESS REPORT\n";
  std::cout << "========================================\n";
  std::cout << "  Passed: " << passed << " / " << queries.size() << "\n";
  double rate = 100.0 * passed / queries.size();
  std::cout << "  Rate:   " << std::fixed << std::setprecision(1) << rate << "%\n";

  double avg_tools = 0, avg_iters = 0, avg_dur = 0;
  int total_dup = 0, total_fail = 0;
  for (auto &e : entries) {
    avg_tools += e.tools.size();
    avg_iters += e.iterations;
    avg_dur += e.duration_ms;
    total_dup += e.duplicate_tools;
    total_fail += e.failed_tools;
  }
  std::cout << "  Avg tools/query:   " << std::setprecision(2) << (avg_tools / queries.size()) << "\n";
  std::cout << "  Avg iterations:    " << std::setprecision(2) << (avg_iters / queries.size()) << "\n";
  std::cout << "  Avg duration:      " << std::setprecision(1) << (avg_dur / queries.size()) << "ms\n";
  std::cout << "  Avg dup tools/q:   " << std::setprecision(2) << (1.0 * total_dup / queries.size()) << "\n";
  std::cout << "  Avg fail tools/q:  " << std::setprecision(2) << (1.0 * total_fail / queries.size()) << "\n";

  std::map<std::string, int> goal_counts;
  for (auto &e : entries) goal_counts[e.goal_type]++;
  std::cout << "\n  GoalType Distribution:\n";
  for (auto &[gt, cnt] : goal_counts)
    std::cout << "    " << gt << ": " << cnt << "\n";

  // === FAILURE REPORT ===
  bool has_failures = false;
  for (auto &e : entries)
    if (e.outcome != "success") { has_failures = true; break; }

  if (has_failures) {
    std::cout << "\n========================================\n";
    std::cout << "  FAILURE REPORT\n";
    std::cout << "========================================\n";

    for (auto &e : entries) {
      if (e.outcome == "success") continue;
      std::cout << "\n  Query:        " << e.query << "\n";
      std::cout << "  GoalType:     " << e.goal_type << "\n";
      std::cout << "  Outcome:      " << e.outcome << "\n";
      std::cout << "  Tools:        ";
      for (auto &t : e.tools) std::cout << t << "; ";
      std::cout << "\n";
      std::cout << "  Failure:      " << e.failure_class << "\n";
      std::cout << "  Recoverable:  " << (e.recoverable ? "Y" : "N") << "\n";
      std::cout << "  Dup Tools:    " << e.duplicate_tools << "\n";
      std::cout << "  Fail Tools:   " << e.failed_tools << "\n";
      std::cout << "  Root Cause:   " << e.root_cause_str << "\n";
      std::cout << "  Repair:       " << e.repair << "\n";
    }
  } else {
    std::cout << "\n  No failures.\n";
  }

  // === AGGREGATE FAILURE CLASS BREAKDOWN ===
  std::map<std::string, int> fc_counts;
  for (auto &e : entries)
    fc_counts[e.failure_class]++;
  std::cout << "\n  Failure Class Breakdown:\n";
  for (auto &[fc, cnt] : fc_counts)
    std::cout << "    " << fc << ": " << cnt << "\n";

  // === BEHAVIOR METRICS ===
  std::cout << "\n========================================\n";
  std::cout << "  BEHAVIOR METRICS (agent loop quality)\n";
  std::cout << "========================================\n";
  std::cout << "  Total queries:            " << queries.size() << "\n";
  std::cout << "  Total tool calls:         " << static_cast<int>(avg_tools) << "\n";
  std::cout << "  Total duplicate tools:    " << total_dup << "\n";
  std::cout << "  Total failed tools:       " << total_fail << "\n";
  std::cout << "  Avg tools/query:          " << std::setprecision(2) << (avg_tools / queries.size()) << "\n";
  std::cout << "  Avg iterations/query:     " << std::setprecision(2) << (avg_iters / queries.size()) << "\n";
  std::cout << "  Avg dup tools/query:      " << std::setprecision(2) << (1.0 * total_dup / queries.size()) << "\n";
  std::cout << "  Avg failed tools/query:   " << std::setprecision(2) << (1.0 * total_fail / queries.size()) << "\n";
  std::cout << "  Duplicate rate:           " << std::setprecision(1) << (100.0 * total_dup / (avg_tools > 0 ? avg_tools : 1)) << "%\n";
  std::cout << "  Failure rate:             " << std::setprecision(1) << (100.0 * total_fail / (avg_tools > 0 ? avg_tools : 1)) << "%\n";

  // Per-query trace table
  std::cout << "\n  Per-Query Efficiency:\n";
  auto save_flags = std::cout.flags();
  std::cout << std::left;
  std::cout << "  " << std::setw(50) << "Query"
            << std::setw(6) << "Tools"
            << std::setw(6) << "Iters"
            << std::setw(6) << "Dup"
            << std::setw(6) << "Fail"
            << std::setw(10) << "Dur(ms)"
            << "\n";
  std::cout << "  " << std::string(84, '-') << "\n";
  for (auto &e : entries) {
    std::string q = e.query;
    if (q.size() > 48) q = q.substr(0, 45) + "...";
    std::cout << "  " << std::setw(50) << q
              << std::setw(6) << e.tools.size()
              << std::setw(6) << e.iterations
              << std::setw(6) << e.duplicate_tools
              << std::setw(6) << e.failed_tools
              << std::setw(10) << std::fixed << std::setprecision(1) << e.duration_ms
              << "\n";
  }
  std::cout.flags(save_flags);

  return has_failures ? 1 : 0;
}
