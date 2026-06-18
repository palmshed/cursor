#include <cstdlib>
#include <iostream>
#include <string>

#include "agent.h"
#include "memory_manager.h"
#include "services/ai_service.h"
#include "utils/config.h"
#include "utils/ui.h"
#include "version.h"

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" || arg == "-v") {
      Version::print_version_info();
      return 0;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: cursor-agent [OPTIONS]\n\n"
                << "Options:\n"
                << "  -v, --version    Print version info and exit\n"
                << "  -h, --help       Show this help and exit\n";
      return 0;
    }
  }

  try {
    Utils::Config::load_environment();
    Utils::UI::print_logo();
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
