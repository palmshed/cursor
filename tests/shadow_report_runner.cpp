#include "services/execution_engine.h"
#include "services/planner_loop.h"
#include "services/evidence_gap_engine.h"
#include "services/file_service.h"
#include "services/find_service.h"
#include "services/symbol_service.h"
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
#include <map>
#include <algorithm>
#include <filesystem>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Per-investigation report entry
// ---------------------------------------------------------------------------
struct ReportEntry {
  std::string query;
  std::string legacy_outcome;
  std::string gap_outcome;
  bool completion_parity; // legacy and gap agree on completion
  std::vector<std::string> legacy_tools;
  int legacy_iterations;
  int planner_steps; // number of shadow steps collected
  int agreements;
  int disagreements;
  int expected_disagreements;
  int unexpected_disagreements;
  bool sequence_identical; // all shadow steps agreed
  std::string gap_detail; // per-requirement gap summary
  std::string planner_stop_reason; // when planner would have stopped
  int planner_stop_at_iteration; // which iteration gap was first complete
};

static std::string first_path(const std::string &grep_out) {
  if (grep_out.empty() || grep_out == "no matches") return {};
  auto colon = grep_out.find(':');
  if (colon == std::string::npos) return {};
  return grep_out.substr(0, colon);
}

// ---------------------------------------------------------------------------
// Is the gap evaluation complete? Re-runs gap engine on ExecutionResult data.
// ---------------------------------------------------------------------------
static bool gap_is_complete(const Services::ExecutionResult &result) {
  if (!result.parsed_goal.goal.is_known())
    return result.success; // fall back to legacy
  auto reqs = Services::ExecutionEngine::evidence_for_goal(result.parsed_goal.goal);
  Services::EvidenceGapEngine gape;
  auto gap = gape.evaluate(reqs, result.evidence);
  return gap.complete();
}

// ---------------------------------------------------------------------------
// Build per-requirement gap summary string
// ---------------------------------------------------------------------------
static std::string build_gap_detail(const Services::ExecutionResult &result) {
  if (!result.parsed_goal.goal.is_known()) return "unknown_goal";
  auto reqs = Services::ExecutionEngine::evidence_for_goal(result.parsed_goal.goal);
  Services::EvidenceGapEngine gape;
  auto gap = gape.evaluate(reqs, result.evidence);
  std::ostringstream oss;
  for (auto &rs : gap.requirements) {
    std::string st = rs.missing() ? "M" :
                     rs.weak() ? "W" :
                     rs.satisfied() ? (rs.complete() ? "C" : "S") : "?";
    oss << static_cast<int>(rs.requirement.ec) << ":" << st;
    if (!rs.complete()) oss << "/q" << static_cast<int>(rs.best_quality)
                           << "/rq" << static_cast<int>(rs.requirement.min_quality);
    oss << " ";
  }
  return oss.str();
}

// ---------------------------------------------------------------------------
// When would the planner have stopped? (first gap-complete iteration)
// ---------------------------------------------------------------------------
static int planner_stop_iteration(const Services::ExecutionResult &result) {
  if (!result.planner_shadow) return -1;
  for (size_t i = 0; i < result.planner_shadow->steps.size(); i++) {
    if (!result.planner_shadow->steps[i].planner_would_continue) {
      // Step at i means gap was already satisfied BEFORE this step's tool
      return (int)i;
    }
  }
  return (int)result.planner_shadow->steps.size(); // never complete
}

// ---------------------------------------------------------------------------
// Build combined query list from benchmark suite + additional patterns
// ---------------------------------------------------------------------------
static std::vector<std::string> build_query_list() {
  std::vector<std::string> queries;

  // Load benchmark suite (look relative to source root and build dir)
  std::ifstream ifs("scenarios/benchmark/benchmark_suite.json");
  if (!ifs) ifs.open("../scenarios/benchmark/benchmark_suite.json");
  if (ifs) {
    json suite;
    ifs >> suite;
    for (auto &q : suite["queries"]) {
      std::string query = q["query"];
      // Avoid duplicates
      if (std::find(queries.begin(), queries.end(), query) == queries.end())
        queries.push_back(query);
    }
  } else {
    std::cerr << "Warning: benchmark_suite.json not found, skipping\n";
  }

  // Architecture review queries
  std::vector<std::string> extra = {
    // Architecture
    "tell me how repository investigation works",
    "explain the telemetry pipeline",
    "how is evidence gating implemented",
    "tell me how the build system works",
    "explain how execution engine works",
    // CI
    "check the ci build status",
    "did the last workflow pass",
    "what is the GitHub actions status",
    // Git
    "what is the last commit",
    "show current git status",
    "what files changed in the last commit",
    "show recent commits",
    "what branch am I on",
    "who made the last commit",
    // Overview
    "what does this project do",
    "show me the project structure",
    "how is the source organized",
    // Navigation
    "go to the execution engine",
    "find the planner loop",
    "find checkpoint service",
    "where is replay implemented",
    "where is TraceConsumer used",
    "where do we call gh run view",
    // Session / State
    "what provider am I using",
    "what model am I using",
    "am I online",
    // Code Change
    "add a new field to session_state",
    "fix compile warnings in auth_service",
    // General Chat
    "hello there",
    "how are you doing",
    // Ambiguous / multi-turn
    "how does it work",
    "what is this",
    // Overview queries
    "review architecture",
    "review codebase",
    "review recent changes",
    // CI Details
    "check run https://github.com/bniladridas/cursor/actions/runs/28139237680",
    "investigate job https://github.com/bniladridas/cursor/actions/runs/28139237680/job/83332734648"
  };

  for (auto &q : extra) {
    if (std::find(queries.begin(), queries.end(), q) == queries.end())
      queries.push_back(q);
  }

  return queries;
}

int main(int, char **) {
  auto queries = build_query_list();
  std::vector<ReportEntry> entries;

  std::string last_grep;
  std::string last_find;
  int total = 0;

  std::cout << "Shadow Planner Report Generator\n";
  std::cout << "================================\n\n";

  for (size_t qi = 0; qi < queries.size(); qi++) {
    auto &query = queries[qi];
    total++;

    try {
      Core::Agent agent;
      Core::UIManager ui(agent);
      Services::ExecutionEngine engine;

      last_grep.clear();
      last_find.clear();

      auto runner = [&](const Services::ToolCall &tc) -> Services::ToolResult {
        Services::ToolResult tr;
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
          tr.out += "SELECTED: " + candidates[0].path + "\nREASON: " + candidates[0].reason + "\nFILES:\n";
          for (auto &c : candidates) tr.out += c.path + "\n";
          return tr;
        }
        if (tc.tool == "references") {
          auto refs = Services::SymbolService::find_references(".", tc.args);
          if (refs.empty()) { tr.out = "no matches"; return tr; }
          last_find = refs[0].file;
          tr.out = Services::SymbolService::format_references(refs);
          return tr;
        }
        if (tc.tool == "grep") {
          auto results = Services::FileService::search_in_directory(
              ".", tc.args.empty() ? query : tc.args, "*");
          results.erase(std::remove_if(results.begin(), results.end(),
              [](const auto &r) {
                return r.file_path.find("benchmark_suite.json") != std::string::npos
                    || r.file_path.find("tests/") != std::string::npos
                    || r.file_path.find("scenarios/") != std::string::npos
                    || r.file_path.find("build/") != std::string::npos;
              }), results.end());
          if (results.empty()) {
            tr.out = "no matches";
          } else {
            std::ostringstream out;
            for (auto &r : results) {
              out << r.file_path << ":" << r.line_number << ": " << r.line_content << "\n";
              if (out.tellp() > 10000) { out << "..."; break; }
            }
            last_grep = out.str();
            tr.out = last_grep;
          }
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
            if (!content.empty()) tr.out += "--- " + f + " ---\n" + content + "\n";
          }
          if (tr.out.empty()) tr.out = "no files to read";
          return tr;
        }
        if (tc.tool == "gh") { tr.out = "[]"; return tr; }
        if (tc.tool == "git") { tr.out = Services::CommandService::execute("git " + tc.args); return tr; }
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

      // Collect legacy information
      std::vector<std::string> tools;
      for (auto &tr : result.tool_history)
        tools.push_back(tr.tool + (tr.args.empty() ? "" : " " + tr.args));

      std::string legacy_outcome = Core::outcome_name(result.outcome);

      // Build gap information from final evidence
      bool gap_complete = gap_is_complete(result);
      std::string gap_outcome = gap_complete ? "success" : legacy_outcome;

      // Completion parity
      bool parity = (result.success == gap_complete);

      // Shadow metrics
      int agree = 0, dis = 0, exp_dis = 0, unexp_dis = 0;
      bool seq_identical = true;
      int planner_stop = planner_stop_iteration(result);

      if (result.planner_shadow) {
        agree = result.planner_shadow->agreements();
        dis = result.planner_shadow->disagreements();
        exp_dis = result.planner_shadow->expected_disagreements();
        unexp_dis = result.planner_shadow->unexpected_disagreements();
        // Sequence identical if all steps agree and planner would have continued
        for (auto &s : result.planner_shadow->steps) {
          if (!s.agreement) { seq_identical = false; break; }
        }
      }

      // Shadow mode -- no pass/fail for individual queries

      std::string gd = build_gap_detail(result);

      entries.push_back({
        query, legacy_outcome, gap_outcome, parity,
        tools, (int)result.tool_history.size(),
        result.planner_shadow ? (int)result.planner_shadow->steps.size() : 0,
        agree, dis, exp_dis, unexp_dis,
        seq_identical, gd,
        (planner_stop >= 0 && planner_stop < (int)result.tool_history.size())
            ? "complete_at_step_" + std::to_string(planner_stop)
            : (planner_stop == -1 ? "no_shadow" : "never_complete"),
        planner_stop
      });

      std::cout << "[" << (qi + 1) << "/" << queries.size() << "] "
                << query << "\n";
      std::cout << "  Legacy: " << legacy_outcome
                << "  Gap: " << gap_outcome
                << (parity ? " parity=ok" : " parity=DIVERGE")
                << "  Tools: " << tools.size()
                << "  Agree: " << agree << "/" << dis
                << " exp=" << exp_dis << " unexp=" << unexp_dis
                << (seq_identical ? " seq=ok" : " seq=diff")
                << " stop=" << (planner_stop >= 0 ? std::to_string(planner_stop) : "?")
                << "\n";

      // Clean up shadow metrics
      delete result.planner_shadow;

    } catch (std::exception &e) {
      std::cout << "[" << (qi + 1) << "/" << queries.size() << "] "
                << query << " ERROR: " << e.what() << "\n";
      entries.push_back({query, "error", "error", false, {}, 0, 0, 0, 0, 0,
                         0, true, "error", "error", -1});
    }
  }

  // ============================
  // AGGREGATE REPORT
  // ============================

  int total_agreements = 0, total_disagreements = 0;
  int total_expected = 0, total_unexpected = 0;
  int seq_identical_count = 0;
  int parity_count = 0;
  int total_legacy_iters = 0, total_planner_steps = 0;
  int planner_complete_early = 0; // planner would stop before legacy

  for (auto &e : entries) {
    total_agreements += e.agreements;
    total_disagreements += e.disagreements;
    total_expected += e.expected_disagreements;
    total_unexpected += e.unexpected_disagreements;
    if (e.sequence_identical) seq_identical_count++;
    if (e.completion_parity) parity_count++;
    total_legacy_iters += e.legacy_iterations;
    total_planner_steps += e.planner_steps;
    if (e.planner_stop_at_iteration >= 0 &&
        e.planner_stop_at_iteration < e.legacy_iterations)
      planner_complete_early++;
  }

  int valid = 0;
  for (auto &e : entries)
    if (e.legacy_outcome != "error") valid++;

  double agree_rate = total + total_disagreements > 0
      ? (100.0 * total_agreements / (total_agreements + total_disagreements))
      : 100.0;
  double seq_rate = valid > 0 ? (100.0 * seq_identical_count / valid) : 0;
  double parity_rate = valid > 0 ? (100.0 * parity_count / valid) : 0;

  std::cout << "\n========================================\n";
  std::cout << "  SHADOW PLANNER REPORT\n";
  std::cout << "========================================\n\n";

  std::cout << "  Investigations: " << valid << " / " << total << "\n\n";

  std::cout << "  Step-level agreement:\n";
  std::cout << "    Total comparisons: "
            << (total_agreements + total_disagreements) << "\n";
  std::cout << "    Agreements:        " << total_agreements << "\n";
  std::cout << "    Disagreements:     " << total_disagreements << "\n";
  std::cout << "    Expected:          " << total_expected << "\n";
  std::cout << "    Unexpected:        " << total_unexpected << "\n";
  std::cout << "    Agreement rate:    "
            << std::fixed << std::setprecision(1) << agree_rate << "%\n\n";

  std::cout << "  Per-investigation:\n";
  std::cout << "    Sequences identical: " << seq_identical_count
            << " (" << std::fixed << std::setprecision(1) << seq_rate << "%)\n";
  std::cout << "    Completion parity:   " << parity_count
            << " (" << std::fixed << std::setprecision(1) << parity_rate << "%)\n";
  std::cout << "    Planner stops early: " << planner_complete_early
            << "\n\n";

  std::cout << "  Average iterations:\n";
  std::cout << "    Legacy:   " << std::fixed << std::setprecision(2)
            << (valid > 0 ? (double)total_legacy_iters / valid : 0) << "\n";
  std::cout << "    Planner:  " << std::fixed << std::setprecision(2)
            << (valid > 0 ? (double)total_planner_steps / valid : 0)
            << " (shadow steps, not actual planner loop)\n\n";

  // Calculate approximate planner V2 iteration count by counting
  // how many steps it would need (steps until gap complete)
  int total_planner_v2_iters = 0;
  int planner_v2_valid = 0;
  for (auto &e : entries) {
    if (e.legacy_outcome == "error") continue;
    // Planner would stop at planner_stop_at_iteration (0 = stop before first tool),
    // so iteration count = planner_stop_at_iteration (not +1 since we count
    // steps that were taken, and stop_at_iteration is the index where stop occurs)
    if (e.planner_stop_at_iteration >= 0) {
      total_planner_v2_iters += e.planner_stop_at_iteration;
      planner_v2_valid++;
    }
  }
  if (planner_v2_valid > 0) {
    std::cout << "    Planner V2: " << std::fixed << std::setprecision(2)
              << (double)total_planner_v2_iters / planner_v2_valid
              << " (estimated from gap completion)\n\n";
  }

  // ============================
  // UNEXPECTED DISAGREEMENT DETAIL
  // ============================

  bool has_unexpected = false;
  for (auto &e : entries) {
    if (e.unexpected_disagreements > 0 || (!e.completion_parity && e.legacy_outcome != "error")) {
      if (!has_unexpected) {
        std::cout << "  UNEXPECTED FINDINGS:\n";
        std::cout << "  ----------------------------------------\n";
        has_unexpected = true;
      }
      std::cout << "\n  --- Query: " << e.query << "\n";
      if (e.unexpected_disagreements > 0) {
        std::cout << "      Unexpected disagreements: "
                  << e.unexpected_disagreements << "\n";
        // Print step details if shadow data available
        std::cout << "      Gap: " << e.gap_detail << "\n";
        std::cout << "      Legacy outcome: " << e.legacy_outcome
                  << "  Gap outcome: " << e.gap_outcome << "\n";
        std::cout << "      Legacy tools: ";
        for (auto &t : e.legacy_tools) std::cout << t << "; ";
        std::cout << "\n";
      }
      if (!e.completion_parity) {
        std::cout << "      Completion DIVERGENCE: "
                  << "legacy=" << e.legacy_outcome
                  << " gap=" << e.gap_outcome << "\n";
      }
    }
  }
  if (!has_unexpected) {
    std::cout << "\n  No unexpected disagreements or completion divergences.\n";
  }

  // ============================
  // OUTCOME DISTRIBUTION
  // ============================
  std::map<std::string, int> legacy_outcomes, gap_outcomes;
  for (auto &e : entries) {
    legacy_outcomes[e.legacy_outcome]++;
    gap_outcomes[e.gap_outcome]++;
  }
  std::cout << "\n  Legacy outcome distribution:\n";
  for (auto &[oc, cnt] : legacy_outcomes)
    std::cout << "    " << oc << ": " << cnt << "\n";

  std::cout << "\n  Gap outcome distribution:\n";
  for (auto &[oc, cnt] : gap_outcomes)
    std::cout << "    " << oc << ": " << cnt << "\n";

  // ============================
  // RECOMMENDATION
  // ============================
  std::cout << "\n  Recommendation:\n";
  if (valid == 0) {
    std::cout << "    No valid investigations. Check configuration.\n";
  } else if (total_unexpected == 0 && parity_rate >= 95.0) {
    std::cout << "    Safe for Release B\n";
    std::cout << "    (0 unexpected disagreements, "
              << std::fixed << std::setprecision(1) << parity_rate
              << "% completion parity)\n";
  } else if (total_unexpected <= 3 && parity_rate >= 90.0) {
    std::cout << "    Proceed with caution -- investigate "
              << total_unexpected << " unexpected findings\n";
  } else {
    std::cout << "    Do not switch. "
              << total_unexpected << " unexpected disagreements, "
              << std::fixed << std::setprecision(1) << (100.0 - parity_rate)
              << "% completion divergence\n";
  }

  // Append to acceptance report
  std::time_t now = std::time(nullptr);
  char timebuf[64];
  std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

  // Get git commit
  std::string commit = "unknown";
  auto commit_result = Services::CommandService::execute("git rev-parse --short HEAD");
  if (!commit_result.empty()) {
    commit = commit_result;
    if (!commit.empty() && commit.back() == '\n') commit.pop_back();
  }

  // Write to source root: try both common build dir locations
  std::string report_path = "../docs/release/planner_acceptance.md";
  if (!std::filesystem::exists("../docs/release/")) {
    report_path = "../../docs/release/planner_acceptance.md";
  }
  std::ofstream report_file(report_path, std::ios::app);
  if (!report_file) {
    // Last resort: create in current dir
    report_path = "planner_acceptance.md";
    report_file.open(report_path, std::ios::app);
  }
  report_file << "\n## Run " << timebuf << "\n\n";
  report_file << "- **Commit**: `" << commit << "`\n";
  report_file << "- **Investigations**: " << valid << " / " << total << "\n";
  report_file << "- **Agreements (step-level)**: " << total_agreements << "\n";
  report_file << "- **Disagreements**: " << total_disagreements << "\n";
  report_file << "  - Expected: " << total_expected << "\n";
  report_file << "  - Unexpected: " << total_unexpected << "\n";
  report_file << "- **Agreement rate**: " << std::fixed << std::setprecision(1) << agree_rate << "%\n";
  report_file << "- **Sequences identical**: " << seq_identical_count << "/" << valid
              << " (" << std::fixed << std::setprecision(1) << seq_rate << "%)\n";
  report_file << "- **Completion parity**: " << parity_count << "/" << valid
              << " (" << std::fixed << std::setprecision(1) << parity_rate << "%)\n";
  report_file << "- **Avg legacy iterations**: " << std::fixed << std::setprecision(2)
              << (valid > 0 ? (double)total_legacy_iters / valid : 0) << "\n";
  if (planner_v2_valid > 0)
    report_file << "- **Avg planner V2 iterations**: " << std::fixed << std::setprecision(2)
                << (double)total_planner_v2_iters / planner_v2_valid << "\n";

  if (total_unexpected == 0)
    report_file << "- **Recommendation**: Safe for Release B\n";
  else
    report_file << "- **Recommendation**: Investigate -- " << total_unexpected << " unexpected\n";
  report_file.close();

  std::cout << "\n  Report appended to docs/release/planner_acceptance.md\n\n";

  return total_unexpected > 0 ? 1 : 0;
}
