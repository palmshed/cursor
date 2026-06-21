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
