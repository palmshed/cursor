#include "app/session.h"
#include "app/command_router.h"
#include "app/common.h"
#include "app/menu.h"
#include "app/startup.h"
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
#include <termios.h>
#include <unistd.h>
#endif

#ifdef CURSOR_USE_LIBEDIT
extern "C" char *readline(const char *);
extern "C" void add_history(const char *);
#endif

// ---------------------------------------------------------------------------
// File-static: read_prompt (terminal input with history)
// ---------------------------------------------------------------------------

static std::string read_prompt(const std::string &prompt,
                               const std::vector<std::string> &history,
                                Core::UIManager &ui) {
  (void)ui;
#ifdef CURSOR_USE_LIBEDIT
  (void)history;
  char *line = readline(prompt.c_str());
  if (line == nullptr) {
    std::cin.setstate(std::ios::eofbit);
    return {};
  }
  std::string input(line);
  if (!input.empty()) {
    add_history(line);
  }
  std::free(line);
  return input;
#else
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
  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  auto redraw = [&]() {
    ui.clear_line();
    std::cout << prompt << buf;
    std::cout << "\r\033[" << ((int)prompt.size() + cursor) << "C"
              << std::flush;
  };

  while (true) {
    int ch = std::cin.get();
    if (ch == EOF || ch == '\n' || ch == '\r') {
      break;
    }
    if (ch == 127 || ch == '\b') {
      if (cursor > 0) {
        buf.erase(cursor - 1, 1);
        cursor--;
        redraw();
      }
    } else if (ch == '\033') {
      if (std::cin.get() == '[') {
        int c = std::cin.get();
        if (c == 'A' && history_index > 0) {
          if (history_index == (int)history.size()) {
            saved = buf;
          }
          history_index--;
          buf = history[history_index];
          cursor = (int)buf.size();
          redraw();
        } else if (c == 'B' && history_index < (int)history.size()) {
          history_index++;
          buf = history_index == (int)history.size() ? saved
                                                     : history[history_index];
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
    } else if (ch >= 32 && ch < 127) {
      buf.insert(cursor, 1, (char)ch);
      cursor++;
      redraw();
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  std::cout << "\n";
  return buf;
#endif
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

  std::string mode_name = Startup::is_online_mode(agent_) ? "online" : "offline";
  std::string model_name = agent_.state_.ollama_model_.empty()
      ? "local"
      : agent_.state_.ollama_model_;
  std::string perm_name;
  switch (agent_.state_.perm_mode_) {
    case Core::PermissionMode::REVIEW: perm_name = "review"; break;
    case Core::PermissionMode::APPLY:  perm_name = "apply";  break;
    case Core::PermissionMode::AGENT:  perm_name = "agent";  break;
  }

  if (tty) {
    std::cout << "\033[90m" << mode_name << " · " << model_name
              << " · " << perm_name
              << Utils::Color::RESET << "\n\n";
  } else {
    ui_.print_ready_interface(mode_name, model_name, perm_name);
  }

  std::vector<std::string> input_history;
  while (true) {
    std::string user_input;
    if (tty) {
      user_input = read_prompt("> ", input_history, ui_);
      if (std::cin.eof()) {
        break;
      }
    } else {
      std::cout << "> " << std::flush;
      if (!std::getline(std::cin, user_input)) {
        break;
      }
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
