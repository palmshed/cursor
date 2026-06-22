#pragma once
#include <string>
#include <vector>

namespace Services {

struct CiWorkflowRun {
  long long id;
  std::string title;
  std::string branch;
  std::string conclusion;
  std::string created_at;
  std::string workflow_name;
};

struct CiFailureDetail {
  long long run_id;
  std::string job_name;
  std::string step_name;
  std::vector<std::string> error_lines;
  std::string likely_file;
};

struct CiInvestigationResult {
  std::string repo;
  bool gh_available;
  std::vector<CiWorkflowRun> recent_runs;
  std::vector<CiFailureDetail> failures;
  std::string summary;
};

class CiInvestigationService {
public:
  static CiInvestigationResult investigate();

  // Step 1: Deterministic failed-run targeting
  // Returns the run ID of the most recent failed workflow run, or 0 if none.
  static long long get_latest_failure_run_id();

  // Step 2: Deterministic failed-step extraction
  // Given a run ID, extract failed jobs and their failed steps via gh run view --json.
  // Returns structured failure details (no error text, just job/step names).
  static std::vector<CiFailureDetail> get_failed_steps(long long run_id);

  // Step 3: Deterministic error-snippet extraction
  // Given a run ID, extract the first meaningful error block from the log.
  static std::string get_error_snippet(long long run_id);

  // Combined: steps 2+3 for a single run ID.
  // Returns a human-readable failure report with job, step, and error snippet.
  static std::string extract_ci_failure(long long run_id);

  // Step 5: Root-cause synthesis (AI layer).
  // If AI is available, returns natural-language root-cause analysis.
  // If AI is unavailable, returns deterministic failure report instead.
  static std::string synthesize_root_cause(long long run_id);

  // Testable: parse raw --json jobs output into structured failures
  static std::vector<CiFailureDetail> parse_failed_steps_json(
      const std::string &json_output, long long run_id);

  static std::string detect_repo();
  static std::string analyze_logs(long long run_id);

private:
  static std::vector<CiWorkflowRun> list_recent_runs(int limit);
  static std::vector<CiFailureDetail> get_failure_details(
      const std::vector<CiWorkflowRun> &runs);
  static std::string suggest_fix(const std::string &error_text);
  static std::string extract_likely_file(const std::string &error_text);
};

} // namespace Services
