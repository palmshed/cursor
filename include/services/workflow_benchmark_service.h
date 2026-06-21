#pragma once
#include "core/metrics.h"
#include <string>
#include <vector>

namespace Services {

struct BenchmarkResult {
  std::string name;
  bool passed;
  std::string details;
  int score;          // 0–100
  int files_touched;
  bool build_ok;
  bool test_ok;
  bool human_needed;
  Core::Outcome outcome{Core::Outcome::InsufficientEvidence};
  Core::RecoveryMetrics recovery_metrics;
  Core::TrustMetrics trust_metrics;
};

class WorkflowBenchmarkService {
public:
  static std::vector<BenchmarkResult> run_all();

  // Happy-path scenarios (1-8)
  static BenchmarkResult scenario_fix_failing_ci();
  static BenchmarkResult scenario_add_cli_command();
  static BenchmarkResult scenario_refactor_service_api();
  static BenchmarkResult scenario_add_unit_tests();
  static BenchmarkResult scenario_investigate_build_failure();
  static BenchmarkResult scenario_update_dependency();
  static BenchmarkResult scenario_find_auth_code();
  static BenchmarkResult scenario_export_history();

  // Recovery scenarios (9-14)
  static BenchmarkResult scenario_recover_broken_cmakelists();
  static BenchmarkResult scenario_recover_broken_github_action();
  static BenchmarkResult scenario_search_miss_authentication();
  static BenchmarkResult scenario_recover_failing_test();
  static BenchmarkResult scenario_missing_dependency();
  static BenchmarkResult scenario_misnamed_config();

private:
  // Fixture helpers
  static std::string create_fixture_dir(const std::string &name);
  static bool verify_file_contains(const std::string &path,
                                    const std::string &content);
  static bool verify_build_passes(const std::string &dir);
  static bool verify_tests_pass(const std::string &dir);
  static void cleanup_fixture(const std::string &dir);
};

} // namespace Services
