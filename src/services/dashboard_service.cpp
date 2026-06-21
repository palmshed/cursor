#include "services/dashboard_service.h"
#include "services/replay_service.h"
#include "utils/ui.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Services {

// ---------------------------------------------------------------------------
// Computed percentages
// ---------------------------------------------------------------------------

double DashboardOutcomeAggregate::success_pct() const {
  return total_events > 0 ? (100.0 * success_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::failure_pct() const {
  return total_events > 0 ? (100.0 * failure_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::insufficient_evidence_pct() const {
  return total_events > 0 ? (100.0 * insufficient_evidence_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::user_rejected_pct() const {
  return total_events > 0 ? (100.0 * user_rejected_count / total_events) : 0.0;
}

double DashboardOutcomeAggregate::user_corrected_goal_pct() const {
  return trust_events > 0 ? (100.0 * user_corrected_goal_count / trust_events) : 0.0;
}

double DashboardOutcomeAggregate::diff_rejected_pct() const {
  return trust_events > 0 ? (100.0 * (trust_events - diff_approved_count) / trust_events) : 0.0;
}

double DashboardOutcomeAggregate::avg_attempts() const {
  return recovery_events > 0 ? (static_cast<double>(total_attempts) / recovery_events) : 0.0;
}

double DashboardOutcomeAggregate::avg_confidence_delta() const {
  return recovery_events > 0 ? (total_confidence_delta / recovery_events) : 0.0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string replay_dir() {
  const char *home = std::getenv("HOME");
  if (!home)
    home = std::getenv("USERPROFILE");
  if (!home)
    home = ".";
  return std::string(home) + "/.cursor/replay";
}

static Core::Outcome parse_outcome(const json &j) {
  std::string name = j.value("outcome", "insufficient_evidence");
  return Core::outcome_from_name(name);
}

static int confidence_band(double v) {
  if (v < 0.2) return 0;
  if (v < 0.4) return 1;
  if (v < 0.6) return 2;
  if (v < 0.8) return 3;
  return 4;
}

// ---------------------------------------------------------------------------
// Generate dashboard
// ---------------------------------------------------------------------------

DashboardOutcomeAggregate DashboardService::generate(
    const std::string &filter) {

  DashboardOutcomeAggregate agg;
  std::string filter_outcome;

  // Parse filter: "outcome=<name>"
  if (!filter.empty()) {
    if (filter.find("outcome=") == 0) {
      filter_outcome = filter.substr(8);
      // Insert underscore before capitals (camelCase → snake_case)
      std::string normalized;
      for (size_t i = 0; i < filter_outcome.size(); i++) {
        if (i > 0 && std::isupper(static_cast<unsigned char>(filter_outcome[i])) &&
            std::islower(static_cast<unsigned char>(filter_outcome[i-1]))) {
          normalized += '_';
        }
        normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(filter_outcome[i])));
      }
      // Normalize spaces/hyphens to underscores
      std::replace(normalized.begin(), normalized.end(), ' ', '_');
      std::replace(normalized.begin(), normalized.end(), '-', '_');
      filter_outcome = normalized;
    }
  }

  std::string dir = replay_dir();
  if (!fs::exists(dir))
    return agg;

  for (auto &entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".log")
      continue;

    std::string session_id = entry.path().stem().string();
    std::ifstream f(entry.path());
    if (!f.is_open())
      continue;

    std::string line;
    bool session_matches_filter = false;
    Core::Outcome last_outcome = Core::Outcome::InsufficientEvidence;
    bool session_saw_ie = false;
    bool session_saw_success = false;

    while (std::getline(f, line)) {
      try {
        auto j = json::parse(line);
        agg.total_events++;

        // Outcome
        Core::Outcome o = Core::Outcome::InsufficientEvidence;
        if (j.contains("outcome")) {
          o = parse_outcome(j);
          last_outcome = o;
          if (o == Core::Outcome::InsufficientEvidence) session_saw_ie = true;
          if (o == Core::Outcome::Success) session_saw_success = true;
        }

        // Execution path
        if (j.contains("execution_path")) {
          auto ep = Core::execution_path_from_name(j.value("execution_path", "unknown"));
          switch (ep) {
            case Core::ExecutionPath::Unknown: agg.unknown_count++; break;
            case Core::ExecutionPath::ChatOnly: agg.chat_only_count++; break;
            case Core::ExecutionPath::Engine: agg.engine_count++; break;
            case Core::ExecutionPath::TaskPipeline: agg.task_pipeline_count++; break;
            case Core::ExecutionPath::DirectService: agg.direct_service_count++; break;
            case Core::ExecutionPath::MetaCommand: agg.meta_command_count++; break;
            case Core::ExecutionPath::ShellEscape: agg.shell_escape_count++; break;
          }
        }

        switch (o) {
          case Core::Outcome::Success:
            agg.success_count++;
            break;
          case Core::Outcome::Failure:
            agg.failure_count++;
            break;
          case Core::Outcome::InsufficientEvidence:
            agg.insufficient_evidence_count++;
            break;
          case Core::Outcome::UserRejected:
            agg.user_rejected_count++;
            break;
        }

        // Check filter match for drill-down
        if (!filter_outcome.empty() &&
            Core::outcome_name(o) == filter_outcome) {
          session_matches_filter = true;
        }

        // Trust metrics
        if (j.contains("trust_metrics")) {
          auto t = j["trust_metrics"];
          agg.trust_events++;
          if (t.value("plan_approved", false))
            agg.plan_approved_count++;
          if (t.value("diff_approved", false))
            agg.diff_approved_count++;
          if (t.value("user_corrected_goal", false))
            agg.user_corrected_goal_count++;
          if (t.value("reverted", false))
            agg.reverted_count++;
        }

        // Recovery metrics + per-tool evidence + cluster classification
        int grep_att = 0, grep_ok = 0, grep_hits = 0;
        if (j.contains("recovery_metrics")) {
          auto r = j["recovery_metrics"];
          agg.recovery_events++;
          agg.total_attempts += r.value("attempts", 0);
          agg.total_confidence_delta +=
              r.value("confidence_delta", 0.0);
          if (r.value("evidence_found", false))
            agg.evidence_found_count++;
          if (r.value("verification_found", false))
            agg.verification_found_count++;

          // Per-tool evidence metrics
          grep_att = r.value("grep_attempts", 0);
          grep_ok = r.value("grep_success", 0);
          int grep_zero = r.value("grep_zero_hit", 0);
          grep_hits = r.value("grep_total_hits", 0);
          int grep_max = r.value("grep_max_hits", 0);
          agg.total_grep_attempts += grep_att;
          agg.total_grep_success += grep_ok;
          agg.total_grep_zero_hit += grep_zero;
          agg.total_grep_hits += grep_hits;
          if (grep_max > agg.max_grep_hits)
            agg.max_grep_hits = grep_max;
          agg.total_read_attempts += r.value("read_attempts", 0);
          agg.total_read_success += r.value("read_success", 0);

          // Search recovery cluster classification
          if (o == Core::Outcome::InsufficientEvidence) {
            if (grep_att > 0) {
              if (grep_ok == 0 && grep_zero > 0) {
                agg.cluster_no_matches++;
              } else if (grep_ok > 0 && grep_hits <= 20) {
                agg.cluster_wrong_matches++;
              } else if (grep_ok > 0 && grep_hits > 20) {
                agg.cluster_too_many_matches++;
              } else {
                agg.cluster_low_confidence++;
              }
            } else {
              agg.cluster_unclassified++;
            }
          }
        }

        // Confidence bands
        double cb = j.value("confidence_before", -1.0);
        double ca = j.value("confidence_after", -1.0);
        bool is_success = (o == Core::Outcome::Success);
        bool is_benchmark = j.value("input", "").find("benchmark:") == 0;
        if (is_benchmark)
          agg.benchmark_events++;
        else
          agg.interactive_events++;

        if (cb >= 0.0) {
          int b = confidence_band(cb);
          agg.band_cb[b]++;
          if (is_success) agg.band_success_cb[b]++;

          if (is_benchmark) {
            agg.band_cb_benchmark[b]++;
            if (is_success) agg.band_success_cb_benchmark[b]++;
          } else {
            agg.band_cb_interactive[b]++;
            if (is_success) agg.band_success_cb_interactive[b]++;
          }
        }
        if (ca >= 0.0) {
          int b = confidence_band(ca);
          agg.band_ca[b]++;
          if (is_success) agg.band_success_ca[b]++;
        }

      } catch (...) {
        // skip malformed lines
      }
    }

    agg.total_sessions++;

    // Query rewording: session had InsufficientEvidence then Success
    if (session_saw_ie && session_saw_success)
      agg.query_rewording_count++;

    // For "outcome=" filter: match if session had any matching event
    // OR if the last event matches
    if (session_matches_filter || 
        (!filter_outcome.empty() && Core::outcome_name(last_outcome) == filter_outcome)) {
      agg.matching_sessions.push_back(session_id);
    }
  }

  return agg;
}

#ifdef _WIN32
#include <io.h>
#include <conio.h>
#include <windows.h>
#define IS_TTY() (_isatty(_fileno(stdin)))
#else
#include <unistd.h>
#include <termios.h>
#define IS_TTY() (isatty(STDIN_FILENO))
#endif

#ifndef _WIN32
static struct termios orig_termios;
static bool raw_mode_active = false;
static void disable_raw_mode() {
  if (raw_mode_active) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_active = false;
  }
}
static void enable_raw_mode() {
  if (!raw_mode_active) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_active = true;
    std::atexit(disable_raw_mode);
  }
}
static int read_keypress() {
  char c;
  int nread = read(STDIN_FILENO, &c, 1);
  if (nread <= 0) return -1;
  if (c == '\033') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) <= 0) return '\033';
    if (read(STDIN_FILENO, &seq[1], 1) <= 0) return '\033';
    if (seq[0] == '[') {
      switch (seq[1]) {
        case 'A': return 'A'; // UP
        case 'B': return 'B'; // DOWN
      }
    }
  }
  return c;
}
#else
static void disable_raw_mode() {}
static void enable_raw_mode() {}
static int read_keypress() {
  int ch = _getch();
  if (ch == 0 || ch == 224) {
    ch = _getch();
    if (ch == 72) return 'A'; // UP
    if (ch == 80) return 'B'; // DOWN
  }
  return ch;
}
#endif

void DashboardService::run_interactive(const std::string &filter) {
  DashboardOutcomeAggregate agg = generate(filter);

  // If no sessions match the outcome filter, just default to non-filtered sessions for navigation
  if (agg.matching_sessions.empty() && filter.empty()) {
    std::string dir = replay_dir();
    if (fs::exists(dir)) {
      for (auto &entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".log") {
          agg.matching_sessions.push_back(entry.path().stem().string());
        }
      }
    }
  }

  // If not running in an interactive terminal, do nothing
  if (!IS_TTY()) {
    return;
  }

  enable_raw_mode();
  size_t selected_idx = 0;
  size_t scroll_offset = 0;
  bool in_details = false;
  std::string selected_session;

  while (true) {
    if (!in_details) {
      // 1. Draw Dashboard
      std::cout << "\033[2J\033[H"; // Clear screen & home cursor

      // Outer Box Header
      std::cout << Utils::Color::CYAN << "╔══════════════════════════════════════════════════════════════════════════════╗\n" << Utils::Color::RESET;
      std::cout << Utils::Color::CYAN << "║" << Utils::Color::BOLD << Utils::Color::GREEN << "                          OUTCOME DASHBOARD & SESSION TUI                     " << Utils::Color::CYAN << "║\n" << Utils::Color::RESET;
      std::cout << Utils::Color::CYAN << "╚══════════════════════════════════════════════════════════════════════════════╝\n" << Utils::Color::RESET;

      // Stats block
      std::cout << "  " << Utils::Color::BOLD << "Total Sessions:" << Utils::Color::RESET << " " << std::left << std::setw(10) << agg.matching_sessions.size()
                << " │ " << Utils::Color::BOLD << "Total Events:" << Utils::Color::RESET << " " << agg.total_events << "\n";
      std::cout << Utils::Color::CYAN << "╟──────────────────────────────────────────────────────────────────────────────╢\n" << Utils::Color::RESET;

      // Outcomes Section
      std::cout << "  " << Utils::Color::BOLD << "Outcome Metrics:" << Utils::Color::RESET << "\n";
      auto draw_bar = [](double pct, const std::string &color) {
        int bar_width = 16;
        int filled = static_cast<int>(pct / 100.0 * bar_width);
        std::string bar = color;
        for (int i = 0; i < filled; i++) bar += "█";
        bar += Utils::Color::DIM;
        for (int i = filled; i < bar_width; i++) bar += "░";
        bar += Utils::Color::RESET;
        return bar;
      };

      std::cout << "    " << Utils::Color::GREEN << "✓ Success" << Utils::Color::RESET << "              "
                << std::fixed << std::setprecision(1) << std::setw(5) << agg.success_pct() << "% (" << std::setw(3) << agg.success_count << ")  "
                << draw_bar(agg.success_pct(), Utils::Color::GREEN) << "\n";
      std::cout << "    " << Utils::Color::RED << "✗ Failure" << Utils::Color::RESET << "              "
                << std::fixed << std::setprecision(1) << std::setw(5) << agg.failure_pct() << "% (" << std::setw(3) << agg.failure_count << ")  "
                << draw_bar(agg.failure_pct(), Utils::Color::RED) << "\n";
      std::cout << "    " << Utils::Color::YELLOW << "○ Insufficient Evidence" << Utils::Color::RESET << " "
                << std::fixed << std::setprecision(1) << std::setw(5) << agg.insufficient_evidence_pct() << "% (" << std::setw(3) << agg.insufficient_evidence_count << ")  "
                << draw_bar(agg.insufficient_evidence_pct(), Utils::Color::YELLOW) << "\n";
      std::cout << "    " << Utils::Color::PINK << "⊝ User Rejected" << Utils::Color::RESET << "         "
                << std::fixed << std::setprecision(1) << std::setw(5) << agg.user_rejected_pct() << "% (" << std::setw(3) << agg.user_rejected_count << ")  "
                << draw_bar(agg.user_rejected_pct(), Utils::Color::PINK) << "\n";

      std::cout << Utils::Color::CYAN << "╟──────────────────────────────────────────────────────────────────────────────╢\n" << Utils::Color::RESET;

      // Execution paths
      std::cout << "  " << Utils::Color::BOLD << "Execution Paths:" << Utils::Color::RESET << "\n";
      std::cout << "    ChatOnly:       " << std::setw(4) << agg.chat_only_count << "  │  Engine:         " << std::setw(4) << agg.engine_count << "\n";
      std::cout << "    Task Pipeline:  " << std::setw(4) << agg.task_pipeline_count << "  │  Meta Command:   " << std::setw(4) << agg.meta_command_count << "\n";
      std::cout << "    Direct Service: " << std::setw(4) << agg.direct_service_count << "  │  Shell Escape:   " << std::setw(4) << agg.shell_escape_count << "\n";

      std::cout << Utils::Color::CYAN << "╟──────────────────────────────────────────────────────────────────────────────╢\n" << Utils::Color::RESET;

      // Scrollable matching sessions list
      std::cout << "  " << Utils::Color::BOLD << "Select Session ID [Use Up/Down Arrow, Enter: View, Q: Quit]:" << Utils::Color::RESET << "\n";

      size_t max_visible = 6;
      if (agg.matching_sessions.empty()) {
        std::cout << "    " << Utils::Color::DIM << "No replay logs found matching outcome filter." << Utils::Color::RESET << "\n";
      } else {
        // Adjust scroll offset
        if (selected_idx >= scroll_offset + max_visible) {
          scroll_offset = selected_idx - max_visible + 1;
        } else if (selected_idx < scroll_offset) {
          scroll_offset = selected_idx;
        }

        for (size_t i = 0; i < max_visible; i++) {
          size_t idx = scroll_offset + i;
          if (idx >= agg.matching_sessions.size()) break;

          if (idx == selected_idx) {
            std::cout << "    " << Utils::Color::BOLD << Utils::Color::CYAN << "➔  "
                      << Utils::Color::GREEN << agg.matching_sessions[idx]
                      << Utils::Color::RESET << "\n";
          } else {
            std::cout << "       " << Utils::Color::DIM << agg.matching_sessions[idx]
                      << Utils::Color::RESET << "\n";
          }
        }
      }
      std::cout << Utils::Color::CYAN << "╚══════════════════════════════════════════════════════════════════════════════╝\n" << Utils::Color::RESET;
      std::cout << std::flush;

      // Handle Key input
      int key = read_keypress();
      if (key == 'q' || key == 'Q' || key == 3) {
        break;
      } else if (key == 'A') { // UP
        if (selected_idx > 0) selected_idx--;
      } else if (key == 'B') { // DOWN
        if (!agg.matching_sessions.empty() && selected_idx + 1 < agg.matching_sessions.size()) selected_idx++;
      } else if (key == '\n' || key == '\r') {
        if (!agg.matching_sessions.empty()) {
          in_details = true;
          selected_session = agg.matching_sessions[selected_idx];
        }
      }
    } else {
      // 2. Draw details of selected session
      std::cout << "\033[2J\033[H";
      std::cout << Utils::Color::CYAN << "╔══════════════════════════════════════════════════════════════════════════════╗\n" << Utils::Color::RESET;
      std::cout << Utils::Color::CYAN << "║" << Utils::Color::BOLD << Utils::Color::GREEN << "                               SESSION LOG VIEWER                              " << Utils::Color::CYAN << "║\n" << Utils::Color::RESET;
      std::cout << Utils::Color::CYAN << "╚══════════════════════════════════════════════════════════════════════════════╝\n" << Utils::Color::RESET;
      std::cout << "  " << Utils::Color::BOLD << "Session ID: " << Utils::Color::CYAN << selected_session << Utils::Color::RESET << "\n";
      std::cout << Utils::Color::CYAN << "╟──────────────────────────────────────────────────────────────────────────────╢\n" << Utils::Color::RESET;

      std::string file_path = replay_dir() + "/" + selected_session + ".log";
      std::ifstream f(file_path);
      if (!f.is_open()) {
        std::cout << "  " << Utils::Color::RED << "Error: Could not open session log file." << Utils::Color::RESET << "\n";
      } else {
        std::string line;
        int step_num = 1;
        while (std::getline(f, line)) {
          try {
            auto j = json::parse(line);
            std::cout << "  " << Utils::Color::CYAN << "● Step " << step_num++ << ":" << Utils::Color::RESET << "\n";
            std::cout << "    Input:      " << Utils::Color::BOLD << j.value("input", "") << Utils::Color::RESET << "\n";

            std::string o = j.value("outcome", "insufficient_evidence");
            std::cout << "    Outcome:    ";
            if (o == "success") std::cout << Utils::Color::GREEN;
            else if (o == "failure") std::cout << Utils::Color::RED;
            else std::cout << Utils::Color::YELLOW;
            std::cout << o << Utils::Color::RESET << "\n";

            std::cout << "    Confidence: " << std::fixed << std::setprecision(2)
                      << j.value("confidence_before", 0.0) << " ➔ "
                      << j.value("confidence_after", 0.0) << "\n";

            if (j.contains("recovery_metrics")) {
              auto rm = j["recovery_metrics"];
              std::cout << "    Recovery:   " << rm.value("attempts", 0) << " attempts, strategy changes: "
                        << rm.value("strategy_changes", 0) << "\n";
            }
            if (j.contains("trust_metrics")) {
              auto tm = j["trust_metrics"];
              std::cout << "    Trust:      "
                        << "plan_approved=" << (tm.value("plan_approved", false) ? "yes" : "no")
                        << ", diff_approved=" << (tm.value("diff_approved", false) ? "yes" : "no")
                        << ", corrected_goal=" << (tm.value("user_corrected_goal", false) ? "yes" : "no")
                        << "\n";
            }
            std::cout << "  ────────────────────────────────────────────────────────────────────────\n";
          } catch (...) {
            // skip malformed lines
          }
        }
      }

      std::cout << "\n  " << Utils::Color::BOLD << "[Press any key to return to dashboard]" << Utils::Color::RESET << "\n";
      std::cout << Utils::Color::CYAN << "╚══════════════════════════════════════════════════════════════════════════════╝\n" << Utils::Color::RESET;
      std::cout << std::flush;

      // Wait for key to return
      read_keypress();
      in_details = false;
    }
  }

  disable_raw_mode();
  std::cout << "\033[2J\033[H" << std::flush; // Clean up screen when exiting
}

} // namespace Services
