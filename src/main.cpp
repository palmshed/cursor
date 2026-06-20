#include <cstdlib>
#include <filesystem>
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
#include "services/replay_service.h"
#include "ui/ui_manager.h"
#include "utils/config.h"
#include "utils/ui.h"
#include "version.h"

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
                << "  --replay <session-id>  Replay a recorded session\n";
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
