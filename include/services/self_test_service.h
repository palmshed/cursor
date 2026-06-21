#pragma once
#include <string>
#include <vector>

namespace Services {

struct ScenarioResult {
  std::string name;
  bool passed;
  std::string details;
};

class SelfTestService {
public:
  static std::vector<ScenarioResult> run_all_scenarios();

private:
  // Workflow scenarios
  static ScenarioResult test_file_create_read_write();
  static ScenarioResult test_file_search();
  static ScenarioResult test_git_workflow();
  static ScenarioResult test_shell_execution();

  // Agent quality scenarios
  static ScenarioResult test_codebase_query_classification();
  static ScenarioResult test_git_status_detection();
  static ScenarioResult test_direct_command_routing();
  static ScenarioResult test_nl_command_mapping();

  static std::string sandbox_dir();
  static bool expect_contains(const std::string &haystack,
                              const std::string &needle);
};

} // namespace Services
