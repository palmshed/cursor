#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "agent.h"
#include "memory_manager.h"
#include "services/ai_service.h"
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
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" || arg == "-v") {
      Version::print_version_info();
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: cursor [OPTIONS]\n\n"
                << "Options:\n"
                << "  -v, --version    Print version info and exit\n"
                << "  -h, --help       Show this help and exit\n"
                << "  --update         Update to latest release\n";
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
  }

  try {
    Utils::Config::load_environment();
    Utils::UI::print_logo();

    std::string latest = Version::check_update();
    if (!latest.empty()) {
      std::cout << Utils::Color::YELLOW
                << "Update available: v" << Version::get_version() << " -> v"
                << latest << "\n"
                << Utils::Color::RESET;
      std::cout << "[1] Update now\n[2] Later\n> ";
      std::string choice;
      std::getline(std::cin, choice);
      if (choice == "1") {
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
