#include "app/session.h"
#include "app/command_router.h"
#include "app/common.h"
#include "app/menu.h"
#include "app/startup.h"
#include "services/ai_service.h"
#include "services/command_service.h"
#include "services/replay_service.h"
#include "utils/ui.h"
#include "version.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// File-static: read_prompt (terminal input with history)
// ---------------------------------------------------------------------------

static std::string read_prompt(const std::string &prompt,
                                const std::vector<std::string> &history,
                                 Core::UIManager &ui) {
  (void)ui;
#ifdef _WIN32
  (void)prompt;
  (void)history;
  std::string input;
  if (!std::getline(std::cin, input)) {
    std::cin.setstate(std::ios::eofbit);
    return {};
  }
  return input;
#else
  std::string buf;
  std::string saved;
  int cursor = 0;
  int history_index = (int)history.size();
  bool esc_pending = false;
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);
  tcflush(STDIN_FILENO, TCIFLUSH);

  // Print the hint line below the input line (dim, clears itself on redraw).
  auto show_hint = [&](const std::string& msg) {
    std::cout << "\n\033[90m" << msg << "\033[0m\033[1A\r"
              << "\033[" << ((int)prompt.size() + cursor) << "C"
              << std::flush;
  };

  auto clear_hint = [&]() {
    std::cout << "\n\033[2K\r\033[1A" << std::flush;
  };

  auto redraw = [&]() {
    // Clear input line and hint line
    std::cout << "\033[2K\r";
    std::cout << "\033[1B\033[2K\r\033[1A";
    // Print prompt and buffer
    std::cout << prompt << buf;
    // Position cursor
    std::cout << "\r\033[" << ((int)prompt.size() + cursor) << "C"
              << std::flush;
  };

  // Draw the initial prompt before waiting for the first keystroke.
  redraw();

  while (true) {
    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0 || ch == '\n' || ch == '\r') {
      clear_hint();
      esc_pending = false;
      if (n <= 0) std::cin.setstate(std::ios::eofbit);
      break;
    }
    if (ch == 127 || ch == '\b') {
      esc_pending = false;
      if (cursor > 0) {
        buf.erase(cursor - 1, 1);
        cursor--;
        redraw();
      }
    } else if (ch == '\033') {
      struct timeval tv = {0, 50000};
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);

      clear_hint();
      int sel_ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
      if (sel_ret > 0) {
        char seq[2] = {0};
        esc_pending = false;
        if (read(STDIN_FILENO, seq, 1) == 1 && seq[0] == '[') {
          if (read(STDIN_FILENO, seq + 1, 1) == 1) {
            int c = seq[1];
            if (c == 'A' && history_index > 0) {
              if (history_index == (int)history.size()) saved = buf;
              history_index--;
              buf = history[history_index];
              cursor = (int)buf.size();
              redraw();
            } else if (c == 'B' && history_index < (int)history.size()) {
              history_index++;
              buf = history_index == (int)history.size() ? saved : history[history_index];
              cursor = (int)buf.size();
              redraw();
            } else if (c == 'D' && cursor > 0) {
              cursor--;
              redraw();
            } else if (c == 'C' && cursor < (int)buf.size()) {
              cursor++;
              redraw();
            }
          }
        } else if (seq[0] >= 32 && seq[0] < 127) {
          buf.insert(cursor, 1, seq[0]);
          cursor++;
          redraw();
        }
      } else {
        if (esc_pending) {
          tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
          std::cout << "\n";
          return "\x1b";
        }
        esc_pending = true;
        show_hint("  press ESC again to go back");
      }
    } else if (ch >= 32 && ch < 127) {
      if (esc_pending) {
        esc_pending = false;
        clear_hint();
      }
      buf.insert(cursor, 1, (char)ch);
      cursor++;
      redraw();
    } else {
      if (esc_pending) {
        esc_pending = false;
        clear_hint();
      }
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  std::cout << "\n";
  return buf;
#endif
}

// ---------------------------------------------------------------------------
// Session implementation
// ---------------------------------------------------------------------------

namespace Core {

Session::Session(Agent &agent, CommandRouter &router, UIManager &ui,
                 Services::ReplayService *replay)
    : agent_(agent), router_(router), ui_(ui), replay_(replay) {}

Session::~Session() = default;

void Session::run() {
  bool tty = is_tty_stream(stdout) && is_tty_stream(stdin);

  if (tty) {
    std::cout << "\n";
    ui_.print_logo();
  }

  Startup startup;
  startup.initialize(agent_);

  // Gather repository context once at startup
  {
    auto &s = agent_.state_;
    try {
      s.cwd_ = std::filesystem::current_path().string();
    } catch (...) { s.cwd_ = "unknown"; }
    s.git_branch_ = Services::CommandService::execute(
        "git rev-parse --abbrev-ref HEAD 2>/dev/null");
    if (!s.git_branch_.empty()) {
      // Strip trailing newline
      auto nl = s.git_branch_.find('\n');
      if (nl != std::string::npos) s.git_branch_ = s.git_branch_.substr(0, nl);
      // Git status
      s.git_status_ = Services::CommandService::execute(
          "git status --short 2>/dev/null");
      // Repo root
      s.repo_root_ = Services::CommandService::execute(
          "git rev-parse --show-toplevel 2>/dev/null");
      if (!s.repo_root_.empty()) {
        auto nl2 = s.repo_root_.find('\n');
        if (nl2 != std::string::npos) s.repo_root_ = s.repo_root_.substr(0, nl2);
      }
    }
    // Build artifacts
    s.build_artifacts_ = Services::CommandService::execute(
        "ls -1 build/bin/ 2>/dev/null || echo '(no build/bin)'");
  }

  const auto& m = agent_.state_.active_model;
  std::string mode_name = Startup::is_online_mode(agent_) ? "online" : "offline";
  std::string provider  = m.display_name;
  std::string model_name = m.api_model;

  if (tty) {
    std::cout << "\033[90m" << mode_name
              << (provider.empty() ? "" : " · " + provider)
              << " · " << model_name
              << Utils::Color::RESET << "\n\n" << std::flush;
  } else {
    ui_.print_ready_interface(mode_name + (provider.empty() ? "" : " · " + provider), model_name);
  }

  std::vector<std::string> input_history;
  while (true) {
    std::string user_input;
    if (tty) {
      user_input = read_prompt("> ", input_history, ui_);
      if (std::cin.eof()) break;

      // Double-ESC sentinel: return to model selection
      if (user_input == "\x1b") {
        std::cout << "\n";
        startup.initialize(agent_);
        // Refresh status line with the newly chosen model
        const auto& m2 = agent_.state_.active_model;
        std::string mn2 = Startup::is_online_mode(agent_) ? "online" : "offline";
        std::cout << "\033[90m" << mn2
                  << " · " << m2.display_name
                  << " · " << m2.api_model
                  << Utils::Color::RESET << "\n\n" << std::flush;
        // Reset the cached AIService so it picks up the new model
        agent_.ai_service_.reset();
        continue;
      }
    } else {
      std::cout << "> " << std::flush;
      if (!std::getline(std::cin, user_input)) break;
    }

    user_input = trim_copy(user_input);
    if (user_input.empty())
      continue;

    if (tty) {
      input_history.push_back(user_input);
    }

    if (user_input == "exit" || user_input == "quit") {
      if (tty)
        std::cout << "\n";
      break;
    }

    auto before = agent_.state_;
    if (user_input == "help" || user_input == "?") {
      router_.process_user_input("/help");
    } else if (user_input == "version") {
      Version::print_version_info();
    } else if (user_input == "update") {
      std::cout << "Checking for updates...\n";
      std::string latest = Version::check_update();
      if (latest.empty()) {
        std::cout << "Already up to date (v" << Version::get_version()
                  << ")\n";
      } else if (Version::download_and_install(latest)) {
        std::cout << "Restart cursor to use the new version.\n";
      }
    } else {
      router_.process_user_input(user_input);
    }

#ifndef _WIN32
    // WAITING_INSPECT: brief keyboard wait for 'i' key after answer
    if (tty && !agent_.state_.verbose_mode_ &&
        agent_.state_.last_investigation.has_value()) {
      struct termios oldt, newt;
      tcgetattr(STDIN_FILENO, &oldt);
      newt = oldt;
      newt.c_lflag &= ~(ICANON | ECHO);
      tcsetattr(STDIN_FILENO, TCSANOW, &newt);
      tcflush(STDIN_FILENO, TCIFLUSH);

      std::cout << "\033[90mPress I to inspect evidence\033[0m\r"
                << std::flush;

      struct timeval tv = {3, 0};
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);
      int sel = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
      if (sel > 0) {
        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) == 1 && (ch == 'i' || ch == 'I')) {
          // Clear the hint line before printing inspect output
          std::cout << "\033[2K\r" << std::flush;
          router_.handle_inspect_command();
        }
      }

      // Clear the hint line
      std::cout << "\033[2K\r" << std::flush;

      tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
#endif
    if (replay_) {
      double cb = before.last_confidence_after;
      double ca = agent_.state_.last_confidence_after;
      replay_->log_input(before, agent_.state_, user_input,
                         agent_.state_.last_outcome,
                         agent_.state_.last_execution_path,
                         agent_.state_.last_recovery_metrics,
                         agent_.state_.last_trust_metrics,
                         cb, ca);
    }
  }

  if (tty) {
    ui_.exit_chat_mode();
  }
  std::cout << "Goodbye\n";
}

} // namespace Core
