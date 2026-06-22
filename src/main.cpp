#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "agent.h"
#include "app/command_router.h"
#include "memory_manager.h"
#include "services/ai_service.h"
#include "services/capability_registry.h"
#include "services/ci_investigation_service.h"
#include "services/dashboard_service.h"
#include "services/workflow_benchmark_service.h"
#include "services/replay_service.h"
#include "services/self_test_service.h"
#include "services/verification_service.h"
#include "ui/ui_manager.h"
#include "utils/config.h"
#include "utils/ui.h"
#include "version.h"
#include "diagnostics/diagnostics.h"

std::string get_exe_path() {
  try {
    return std::filesystem::canonical("/proc/self/exc").string();
  } catch (...) {
  }
#ifdef __APPLE__
  char buf[1024];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) {
    return std::filesystem::canonical(buf).string();
  }
#endif
  return {};
}

int main(int argc, char *argv[]) {
  std::string replay_session_id;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" || arg == "-v") {
      Version::print_version_info();
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: cursor [OPTIONS]\n\n"
                << "Options:\n"
                << "  -v, --version         Print version info and exit\n"
                << "  -h, --help            Show this help and exit\n"
                << "  --update              Update to latest release\n"
                << "  --replay <session-id>  Replay a recorded session\n"
                << "  --doctor              Run capability verification\n"
                << "  --self-test           Run automated workflow tests\n"
                << "  --capabilities        List all capabilities\n"
                << "  --self-test-workflows Run workflow benchmark scenarios\n"
                << "  --benchmark           Run workflow benchmark scenarios (alias)\n"
                << "  --ci-investigate      Investigate recent CI failures\n"
                << "  --dashboard [filter]  Show outcome dashboard (e.g. outcome=user_rejected)\n"
                << "  --calibrate           Show confidence calibration analysis\n"
                << "  --json <prompt>       Single-prompt JSON diagnostics (with files_examined)\n"
                << "  --trace <file> <prompt>  Run with tool tracing, write trace.json\n"
                << "  --stream-report <file> <cmd>  Run command with streaming telemetry\n"
                << "  --scenario <file>     Run scenario JSON and verify expected outcome\n";
      return 0;
    }
    if (arg == "--update") {
      std::string latest = Version::check_update();
      if (latest.empty()) {
        std::cout << "Already up to date (v" << Version::get_version()
                  << ")\n";
        return 0;
      }
      return Version::download_and_install(latest) ? 0 : 1;
    }
    if (arg == "--replay" && i + 1 < argc) {
      replay_session_id = argv[++i];
    }
    if (arg == "--doctor") {
      Utils::Config::load_environment();
      auto results = Services::VerificationService::run_all_checks();
      int passed = 0, failed = 0;
      for (auto &r : results) {
        if (r.passed) { passed++; std::cout << "  \u2713 "; }
        else { failed++; std::cout << "  \u2717 "; }
        std::cout << r.name;
        if (!r.details.empty()) std::cout << "  " << r.details;
        std::cout << "\n";
        if (!r.fix_suggestion.empty())
          std::cout << "     " << r.fix_suggestion << "\n";
      }
      std::cout << "\n  " << passed << " passed, " << failed << " failed\n";
      return failed > 0 ? 1 : 0;
    }
    if (arg == "--self-test") {
      Utils::Config::load_environment();
      auto results = Services::SelfTestService::run_all_scenarios();
      int passed = 0, failed = 0;
      for (auto &r : results) {
        if (r.passed) { passed++; std::cout << "  \u2713 "; }
        else { failed++; std::cout << "  \u2717 "; }
        std::cout << r.name;
        if (!r.details.empty()) std::cout << "  " << r.details;
        std::cout << "\n";
      }
      std::cout << "\n  " << passed << " passed, " << failed << " failed\n";
      return failed > 0 ? 1 : 0;
    }
    if (arg == "--capabilities") {
      Utils::Config::load_environment();
      auto caps = Services::CapabilityRegistry::list_all();
      int available = 0, unavailable = 0;
      for (auto &c : caps) {
        if (c.available) { available++; std::cout << "  \u2713 "; }
        else { unavailable++; std::cout << "  \u2717 "; }
        std::cout << c.name;
        if (!c.description.empty())
          std::cout << "  " << Utils::Color::DIM << c.description << Utils::Color::RESET;
        std::cout << "\n";
      }
      std::cout << "\n  " << available << " available, " << unavailable << " unavailable\n";
      return unavailable > 0 ? 1 : 0;
    }
    if (arg == "--self-test-workflows") {
      Utils::Config::load_environment();
      auto results = Services::WorkflowBenchmarkService::run_all();
      int passed = 0, failed = 0;
      int total_score = 0, max_score = 0;
      for (auto &r : results) {
        if (r.passed) { passed++; }
        else { failed++; }
        total_score += r.score;
        max_score += 100;
        std::cout << (r.passed ? "  \u2713 " : "  \u2717 ") << r.name;
        if (!r.details.empty())
          std::cout << "  " << r.details;
        std::cout << "\n";
      }
      std::cout << "\n  Scenarios: " << passed << "/" << (passed + failed) << " passed\n";
      std::cout << "  Score: " << total_score << "/" << max_score << "\n";
      return failed > 0 ? 1 : 0;
    }
    if (arg == "--benchmark") {
      Utils::Config::load_environment();
      auto results = Services::WorkflowBenchmarkService::run_all();

      // Log benchmark results to replay with heuristic confidence
      Services::ReplayService replay;
      Core::SessionState bench_state;
      for (auto &r : results) {
        bench_state.command_count_++;
        double conf_before = 0.5;
        double conf_after =
            r.outcome == Core::Outcome::Success ? 1.0 :
            r.outcome == Core::Outcome::Failure ? 0.3 : 0.0;
        bench_state.last_confidence_before = conf_before;
        bench_state.last_confidence_after = conf_after;
        replay.log_input(bench_state, bench_state,
                         "benchmark:" + r.name, r.outcome,
                         Core::ExecutionPath::Engine,
                         r.recovery_metrics, r.trust_metrics,
                         conf_before, conf_after);
      }

      int passed = 0, failed = 0;
      int total_score = 0, max_score = 0;

      std::cout << "\n--- Workflow Benchmarks ---\n";
      for (auto &r : results) {
        if (r.passed) passed++; else failed++;
        total_score += r.score;
        max_score += 100;

        std::cout << "  " << (r.passed ? "\u2713" : "\u2717") << " " << r.name;
        if (!r.details.empty())
          std::cout << "  " << r.details;
        std::cout << "\n";
      }
      std::cout << "\n  Scenarios: " << passed << "/" << (passed + failed) << " passed\n";
      std::cout << "  Score: " << total_score << "/" << max_score << "\n\n";
      return failed > 0 ? 1 : 0;
    }
    if (arg == "--ci-investigate") {
      Utils::Config::load_environment();
      std::cout << "\n--- CI Investigation ---\n";
      auto result = Services::CiInvestigationService::investigate();
      if (!result.gh_available) {
        std::cout << "  " << result.summary << "\n";
        return 1;
      }
      std::cout << "  Repo: " << result.repo << "\n";
      std::cout << "  Recent runs: " << result.recent_runs.size() << "\n";
      for (auto &r : result.recent_runs) {
        std::string icon = (r.conclusion == "success") ? "\u2713" :
                           (r.conclusion == "failure") ? "\u2717" : "\u2014";
        std::cout << "    " << icon << "  #" << r.id << "  "
                  << r.title << "  (" << r.branch << ")"
                  << "  " << r.conclusion << "\n";
      }
      if (!result.failures.empty()) {
        std::cout << "  Failures:\n";
        for (auto &f : result.failures) {
          std::cout << "    Run #" << f.run_id << "\n";
          for (auto &err : f.error_lines) {
            std::cout << "      " << err << "\n";
          }
          if (!f.likely_file.empty())
            std::cout << "      File: " << f.likely_file << "\n";
        }
      }
      return result.failures.empty() ? 0 : 1;
    }
    if (arg == "--diagnostics") {
      std::string prompt;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        prompt = argv[++i];
      } else {
        std::cerr << "Missing prompt for --diagnostics\n";
        return 2;
      }
      return run_diagnostics(prompt);
    }
    if (arg == "--json") {
      std::string prompt;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        prompt = argv[++i];
      } else {
        std::cerr << "Missing prompt for --json\n";
        return 2;
      }
      Utils::Config::load_environment();
      return run_json_query(prompt);
    }
    if (arg == "--trace") {
      std::string trace_path;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        trace_path = argv[++i];
      } else {
        std::cerr << "Usage: --trace <output.json> <prompt>\n";
        return 2;
      }
      std::string prompt;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        prompt = argv[++i];
      } else {
        std::cerr << "Missing prompt for --trace\n";
        return 2;
      }
      Utils::Config::load_environment();
      return run_trace_query(prompt, trace_path);
    }
    if (arg == "--stream-report") {
      std::string report_path;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        report_path = argv[++i];
      } else {
        std::cerr << "Usage: --stream-report <report.json> <command>\n";
        return 2;
      }
      std::string command;
      if (i + 1 < argc) {
        // Collect remaining args as the command
        command = argv[++i];
        while (i + 1 < argc) {
          command += " ";
          command += argv[++i];
        }
      } else {
        std::cerr << "Missing command for --stream-report\n";
        return 2;
      }
      return run_stream_report(command, report_path);
    }
    if (arg == "--scenario") {
      std::string scenario_path;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        scenario_path = argv[++i];
      } else {
        std::cerr << "Usage: --scenario <scenario.json>\n";
        return 2;
      }
      Utils::Config::load_environment();
      return run_scenario(scenario_path);
    }
    if (arg == "--dashboard") {


      std::string filter;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        filter = argv[++i];
      }
      Utils::Config::load_environment();
      auto agg = Services::DashboardService::generate(filter);

      std::cout << "\n--- Outcome Dashboard ---\n";
      std::cout << "  Sessions: " << agg.total_sessions << "\n";
      std::cout << "  Events:   " << agg.total_events << "\n\n";
      std::cout << "  Outcomes\n";
      std::cout << "  --------\n";
      std::cout << "  Success               " << std::fixed << std::setprecision(1)
                << agg.success_pct() << "%  (" << agg.success_count << ")\n";
      std::cout << "  Failure               " << std::fixed << std::setprecision(1)
                << agg.failure_pct() << "%  (" << agg.failure_count << ")\n";
      std::cout << "  Insufficient Evidence " << std::fixed << std::setprecision(1)
                << agg.insufficient_evidence_pct() << "%  (" << agg.insufficient_evidence_count << ")\n";
      std::cout << "  User Rejected         " << std::fixed << std::setprecision(1)
                << agg.user_rejected_pct() << "%  (" << agg.user_rejected_count << ")\n\n";

      std::cout << "  Execution Path\n";
      std::cout << "  --------------\n";
      std::cout << "  Unknown               " << agg.unknown_count << "\n";
      std::cout << "  ChatOnly              " << agg.chat_only_count << "\n";
      std::cout << "  Engine                " << agg.engine_count << "\n";
      std::cout << "  Task Pipeline         " << agg.task_pipeline_count << "\n";
      std::cout << "  Direct Service        " << agg.direct_service_count << "\n";
      std::cout << "  Meta Command          " << agg.meta_command_count << "\n";
      std::cout << "  Shell Escape          " << agg.shell_escape_count << "\n\n";

      if (agg.trust_events > 0) {
        std::cout << "  Trust Metrics\n";
        std::cout << "  -------------\n";
        std::cout << "  user_corrected_goal   "
                  << agg.user_corrected_goal_count << "  ("
                  << std::fixed << std::setprecision(1)
                  << agg.user_corrected_goal_pct() << "% of trust events)\n";
        std::cout << "  diff_approved=false   "
                  << (agg.trust_events - agg.diff_approved_count) << "  ("
                  << std::fixed << std::setprecision(1)
                  << agg.diff_rejected_pct() << "% of trust events)\n";
        std::cout << "  reverted              "
                  << agg.reverted_count << "\n\n";
      }

      if (agg.recovery_events > 0) {
        std::cout << "  Recovery\n";
        std::cout << "  --------\n";
        std::cout << "  avg attempts          " << std::fixed << std::setprecision(1)
                  << agg.avg_attempts() << "\n";
        std::cout << "  avg confidence delta  " << std::fixed << std::setprecision(2)
                  << agg.avg_confidence_delta() << "\n";
        if (agg.total_grep_attempts > 0 || agg.total_read_attempts > 0) {
          std::cout << "\n  Evidence Metrics\n";
          std::cout << "  -----------------\n";
          if (agg.total_grep_attempts > 0) {
            std::cout << "  grep attempts         " << agg.total_grep_attempts
                      << "  (" << agg.total_grep_success << " ok, "
                      << agg.total_grep_zero_hit << " no matches)\n";
            double avg_files = agg.recovery_events > 0
                ? static_cast<double>(agg.total_grep_hits) / agg.recovery_events
                : 0.0;
            std::cout << "  avg files examined    " << std::fixed << std::setprecision(1)
                      << avg_files << "\n";
            std::cout << "  max grep hits         " << agg.max_grep_hits << "\n";
          }
          if (agg.total_read_attempts > 0)
            std::cout << "  read attempts         " << agg.total_read_attempts
                      << "  (" << agg.total_read_success << " ok, "
                      << (agg.total_read_attempts - agg.total_read_success) << " empty)\n";
        }
      }

      if (agg.insufficient_evidence_count > 0) {
        int total_clusters = agg.cluster_no_matches + agg.cluster_wrong_matches +
                             agg.cluster_too_many_matches + agg.cluster_low_confidence +
                             agg.cluster_unclassified;
        std::cout << "\n  Search Recovery Clusters\n";
        std::cout << "  --------------------------\n";
        std::cout << "  no matches            "
                  << agg.cluster_no_matches << "  ("
                  << std::fixed << std::setprecision(1)
                  << (total_clusters > 0 ? 100.0 * agg.cluster_no_matches / total_clusters : 0.0) << "%)\n";
        std::cout << "  wrong matches         "
                  << agg.cluster_wrong_matches << "  ("
                  << (total_clusters > 0 ? 100.0 * agg.cluster_wrong_matches / total_clusters : 0.0) << "%)\n";
        std::cout << "  too many matches      "
                  << agg.cluster_too_many_matches << "  ("
                  << (total_clusters > 0 ? 100.0 * agg.cluster_too_many_matches / total_clusters : 0.0) << "%)\n";
        std::cout << "  low confidence        "
                  << agg.cluster_low_confidence << "  ("
                  << (total_clusters > 0 ? 100.0 * agg.cluster_low_confidence / total_clusters : 0.0) << "%)\n";
        std::cout << "  unclassified*         "
                  << agg.cluster_unclassified << "  ("
                  << (total_clusters > 0 ? 100.0 * agg.cluster_unclassified / total_clusters : 0.0) << "%)\n";
        if (agg.cluster_unclassified > 0) {
          std::cout << "  *per-tool grep/read counters began collection as of last commit;\n"
                    << "   historical replay events default to 0 and cannot be classified.\n";
        }
      }

      if (agg.query_rewording_count > 0) {
        std::cout << "\n  Query Rewording\n";
        std::cout << "  -----------------\n";
        std::cout << "  sessions with IE→Success "
                  << agg.query_rewording_count << "\n";
      }

      if (!agg.matching_sessions.empty()) {
        std::cout << "  Matching sessions (" << agg.matching_sessions.size() << "):\n";
        for (auto &id : agg.matching_sessions) {
          std::cout << "    " << id << "\n";
        }
        std::cout << "\n";
      }

      if (!filter.empty() && agg.matching_sessions.empty()) {
        std::cout << "  No sessions match filter: " << filter << "\n\n";
      }

      std::cout << "  Every dashboard number can be traced to concrete replay evidence.\n";
      std::cout << "  Drill-down: cursor --dashboard outcome=<name>\n\n";
      return 0;
    }
    if (arg == "--calibrate") {
      Utils::Config::load_environment();
      auto agg = Services::DashboardService::generate();

      std::cout << "\n--- Confidence Calibration ---\n\n";

      if (agg.total_events == 0) {
        std::cout << "  No events recorded yet. Run benchmarks first.\n";
        std::cout << "  Usage: cursor --benchmark\n\n";
        return 0;
      }

      // Compute confidence band success rates from outcome data
      // Outcome distribution calibration
      std::cout << "  Outcome Distribution\n";
      std::cout << "  --------------------\n";
      auto print_outcome_line = [](const std::string &label, int count, int total, const std::string &note) {
        if (count == 0) return;
        double pct = total > 0 ? 100.0 * count / total : 0.0;
        std::cout << "  " << std::left << std::setw(38) << label
                  << std::right << std::setw(6) << count
                  << std::setw(7) << std::fixed << std::setprecision(1) << pct << "%"
                  << "  " << note << "\n";
      };
      print_outcome_line("Success", agg.success_count, agg.total_events, "capability validated");
      print_outcome_line("Failure", agg.failure_count, agg.total_events, "capability insufficient");
      print_outcome_line("InsufficientEvidence", agg.insufficient_evidence_count, agg.total_events, "judgment worked");
      print_outcome_line("UserRejected", agg.user_rejected_count, agg.total_events, "goal understanding failed");
      std::cout << "\n";

      // Confidence band calibration (confidence_before)
      bool has_cb = false;
      for (int j = 0; j < 5; j++) if (agg.band_cb[j] > 0) has_cb = true;

      if (has_cb) {
        const char *band_labels[5] = {"0.0-0.2", "0.2-0.4", "0.4-0.6", "0.6-0.8", "0.8-1.0"};
        std::cout << "  Calibration by confidence_before\n";
        std::cout << "  --------------------------------\n";
        std::cout << "  Band       Events   Success   Rate\n";
        for (int j = 0; j < 5; j++) {
          if (agg.band_cb[j] == 0) continue;
          double rate = 100.0 * agg.band_success_cb[j] / agg.band_cb[j];
          std::cout << "  " << band_labels[j]
                    << std::right << std::setw(10) << agg.band_cb[j]
                    << std::setw(9) << agg.band_success_cb[j]
                    << std::setw(7) << std::fixed << std::setprecision(1) << rate << "%\n";
        }
        std::cout << "\n";
      }

      if (!has_cb) {
        std::cout << "  Confidence calibration requires events with confidence_before set.\n";
        std::cout << "  Accumulate replay sessions with confidence data and re-run.\n\n";
      }

      // Separate breakdown by query type
      auto print_band_breakdown = [&](const char *label, const int *events, const int *successes, int count) {
        bool has_data = false;
        for (int i = 0; i < 5; i++) if (events[i] > 0) has_data = true;
        if (!has_data) return;
        const char *band_labels[5] = {"0.0-0.2", "0.2-0.4", "0.4-0.6", "0.6-0.8", "0.8-1.0"};
        std::string dash_line(35, '-');
        std::cout << "  " << label << "  (" << count << " events)\n";
        std::cout << "  " << dash_line << "\n";
        std::cout << "  Band       Events   Success   Rate\n";
        for (int i = 0; i < 5; i++) {
          if (events[i] == 0) continue;
          double rate = 100.0 * successes[i] / events[i];
          std::cout << "  " << band_labels[i]
                    << std::right << std::setw(10) << events[i]
                    << std::setw(9) << successes[i]
                    << std::setw(7) << std::fixed << std::setprecision(1) << rate << "%\n";
        }
      };
      std::cout << "\n";
      print_band_breakdown("Interactive", agg.band_cb_interactive, agg.band_success_cb_interactive, agg.interactive_events);
      std::cout << "\n";
      print_band_breakdown("Benchmark", agg.band_cb_benchmark, agg.band_success_cb_benchmark, agg.benchmark_events);
      std::cout << "\n";
      return 0;
    }
  }

  try {
    Utils::Config::load_environment();

    if (!replay_session_id.empty()) {
      Services::ReplayService replay;
      auto events = replay.load_session(replay_session_id);
      if (events.empty()) {
        std::cerr << "Replay session not found: " << replay_session_id
                  << "\n";
        return 1;
      }
      Core::Agent agent;
      Core::UIManager ui(agent);
      Core::CommandRouter router(agent, ui);
      std::cout << "Replaying " << events.size() << " steps...\n\n";
      for (auto &ev : events) {
        std::cout << "[" << ev.step << "] " << ev.input << "\n";
        router.process_user_input(ev.input);
        std::cout << "\n";
      }
      std::cout << "Replay complete.\n";
      return 0;
    }

    std::string latest;
    if (!Utils::Config::has_env_var("CURSOR_SKIP_UPDATE_CHECK")) {
      latest = Version::check_update();
    }
    if (!latest.empty()) {
      std::vector<std::string> items = {"Update now", "Later"};
      std::string title = std::string("Update available: v") + Version::get_version() + " \xe2\x86\x92 v" + latest;
      int choice = Core::show_menu(title, items, 1);
      if (choice == 0) {
        if (Version::download_and_install(latest)) {
          std::string exe = get_exe_path();
          if (!exe.empty()) {
#ifndef _WIN32
            execl(exe.c_str(), exe.c_str(), (char *)NULL);
#endif
          }
        }
        return 0;
      }
    }

    Core::Agent agent;
    agent.run();
    std::cout << "Agent run completed" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << Utils::Color::RED << "Fatal error: " << e.what()
              << Utils::Color::RESET << std::endl;
    return 1;
  }

  return 0;
}
