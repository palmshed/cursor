#include "diagnostics/diagnostics.h"
#include "utils/config.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
  std::string scenarios_dir = ".";
  if (argc > 1) {
    scenarios_dir = argv[1];
  }

  if (!fs::is_directory(scenarios_dir)) {
    std::cerr << "Usage: cursor-tester <scenarios-dir>\n";
    std::cerr << "Not a directory: " << scenarios_dir << "\n";
    return 1;
  }

  Utils::Config::load_environment();

  std::vector<std::string> scenario_files;
  for (auto &entry : fs::recursive_directory_iterator(scenarios_dir)) {
    if (entry.path().extension() == ".json") {
      scenario_files.push_back(entry.path().string());
    }
  }

  if (scenario_files.empty()) {
    std::cout << "No scenario files (*.json) found in " << scenarios_dir << "\n";
    return 0;
  }

  std::sort(scenario_files.begin(), scenario_files.end());

  int passed = 0;
  int failed = 0;
  std::vector<std::string> failed_names;

  for (auto &path : scenario_files) {
    std::string name = fs::path(path).filename().string();
    std::cout << "  " << name << " … " << std::flush;
    int rc = run_scenario(path);
    if (rc == 0) {
      std::cout << "\r  PASS " << name << "\n";
      passed++;
    } else {
      std::cout << "\r  FAIL " << name << "\n";
      failed++;
      failed_names.push_back(name);
    }
  }

  std::cout << "\n" << (passed + failed) << " scenarios: "
            << passed << " passed, " << failed << " failed\n";

  if (!failed_names.empty()) {
    std::cout << "\nFailed scenarios:\n";
    for (auto &n : failed_names) {
      std::cout << "  " << n << "\n";
    }
  }

  return failed > 0 ? 1 : 0;
}
