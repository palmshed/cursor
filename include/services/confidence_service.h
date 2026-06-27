#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace Services {

struct CategoryBreakdown {
  std::string category;
  int entry_count;
  double max_score;
  double min_score;
  double agreement;       // 0.0 (contradictory) to 1.0 (consistent)
  double effective_score; // max_score adjusted by agreement
  double weight;          // category weight in the combine formula
  double contribution;    // weight * effective_score
};

struct ConfidenceResult {
  double score;            // 0.0 to 1.0
  std::string reason;      // human-readable explanation
  std::vector<std::string> evidence;  // what we know
  std::vector<std::string> gaps;      // what we don't know
  std::string category;    // evidence category tag
  std::string target;      // tool target/query for convergence detection
  std::vector<CategoryBreakdown> breakdown;  // per-category breakdown for calibration
};

class ConfidenceService {
public:
  // After search/grep
  static ConfidenceResult after_search(const std::string &query,
                                        int results_found);

  // After reading files
  static ConfidenceResult after_read(int files_read,
                                      bool relevant_to_goal);

  // After build
  static ConfidenceResult after_build(bool build_passed,
                                       const std::string &error_summary);

  // After tests
  static ConfidenceResult after_tests(int tests_run,
                                       int tests_passed,
                                       int tests_failed);

  // After CI investigation
  static ConfidenceResult after_ci(bool logs_available,
                                     bool workflow_found,
                                     bool failure_identified);

  // After discovery
  static ConfidenceResult after_discovery(int source_files,
                                            int services_found,
                                            bool has_tests,
                                            bool has_ci);

  // Combine multiple confidence results into category-weighted score
  static ConfidenceResult combine(const std::vector<ConfidenceResult> &results);

  // Should the agent proceed?
  static bool should_proceed(const ConfidenceResult &confidence,
                               double threshold = 0.5);

  // Should the agent stop and ask for help?
  static bool should_stop(const ConfidenceResult &confidence,
                            double threshold = 0.3);

private:
  // Category weights (sum to 1.0)
  static const std::unordered_map<std::string, double> &category_weights();
};

} // namespace Services
