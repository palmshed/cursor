#include "diagnostics/diagnostics.h"
#include "utils/config.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
  std::string scenarios_dir = "scenarios";
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

  // Track per-directory counts: {category -> (passed, total)}
  std::map<std::string, std::pair<int, int>> dir_stats;
  int passed = 0;
  int failed = 0;
  std::vector<std::string> failed_names;

  for (auto &path : scenario_files) {
    // Extract the immediate subdirectory as the category
    fs::path rel = fs::relative(path, scenarios_dir);
    std::string category = ".";
    if (rel.has_parent_path()) {
      category = rel.parent_path().string();
      // If nested deeper than one level, take the topmost subdir
      auto slash = category.find('/');
      if (slash == std::string::npos)
        slash = category.find('\\');
      if (slash != std::string::npos)
        category = category.substr(0, slash);
    }
    if (dir_stats.find(category) == dir_stats.end())
      dir_stats[category] = {0, 0};

    std::string name = fs::path(path).filename().string();
    std::cout << "  " << name << "  " << std::flush;
    int rc = run_scenario(path);
    if (rc == 0) {
      std::cout << "\r  PASS  " << name << "\n";
      passed++;
      dir_stats[category].first++;
    } else {
      std::cout << "\r  FAIL  " << name << "\n";
      failed++;
      failed_names.push_back(name);
    }
    dir_stats[category].second++;
  }

  std::cout << "\n";
  std::cout << "============================================\n";
  std::cout << "  Scenario Summary\n";
  std::cout << "============================================\n";
  int regression_total = 0;
  int regression_passed = 0;
  for (auto &[cat, stats] : dir_stats) {
    if (cat == "regressions") {
      regression_total = stats.second;
      regression_passed = stats.first;
    }
    std::string label = cat == "commands" ? "Commands" :
                        cat == "codebase" ? "Codebase" :
                        cat == "ci"       ? "CI" :
                        cat == "repository" ? "Repository" :
                        cat == "diagnostics" ? "Diagnostics" :
                        cat == "telemetry" ? "Telemetry" :
                        cat == "evidence" ? "Evidence" :
                        cat == "failures" ? "Failures" :
                        cat == "regressions" ? "Regressions" :
                        cat == "streaming" ? "Streaming" :
                        cat;
    std::cout << "  " << label << ": "
              << stats.first << "/" << stats.second << " passed\n";
  }
  if (regression_total > 0) {
    std::cout << "--------------------------------------------\n";
    std::cout << "  Regression Scenarios: " << regression_passed << "/"
              << regression_total << "\n";
  }
  std::cout << "  Total: " << (passed + failed) << " scenarios, "
            << passed << " passed, " << failed << " failed\n";
  std::cout << "============================================\n";

  if (!failed_names.empty()) {
    std::cout << "\nFailed scenarios:\n";
    for (auto &n : failed_names) {
      std::cout << "  " << n << "\n";
    }
  }

  return failed > 0 ? 1 : 0;
}
