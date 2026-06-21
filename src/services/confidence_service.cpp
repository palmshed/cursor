#include "services/confidence_service.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Services {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string fmt_score(double s) {
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(2);
  os << s;
  return os.str();
}

// ---------------------------------------------------------------------------
// Search confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_search(
    const std::string &query, int results_found) {

  ConfidenceResult r;

  if (results_found == 0) {
    r.score = 0.10;
    r.reason = "no results for '" + query + "'";
    r.gaps.push_back("no matches found");
    r.evidence.push_back("searched: " + query);
  } else if (results_found <= 3) {
    r.score = 0.60;
    r.reason = std::to_string(results_found) + " results found";
    r.evidence.push_back(std::to_string(results_found) + " matches for: " + query);
  } else if (results_found <= 10) {
    r.score = 0.80;
    r.reason = std::to_string(results_found) + " results found";
    r.evidence.push_back(std::to_string(results_found) + " matches for: " + query);
  } else {
    r.score = 0.90;
    r.reason = std::to_string(results_found) + " results found";
    r.evidence.push_back(std::to_string(results_found) + " matches for: " + query);
  }

  return r;
}

// ---------------------------------------------------------------------------
// Read confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_read(int files_read,
                                                bool relevant_to_goal) {
  ConfidenceResult r;

  if (files_read == 0) {
    r.score = 0.10;
    r.reason = "no files read";
    r.gaps.push_back("no files read");
  } else if (files_read == 1) {
    r.score = relevant_to_goal ? 0.55 : 0.35;
    r.reason = "1 file read" +
               std::string(relevant_to_goal ? " (relevant)" : " (may not be relevant)");
    r.evidence.push_back("read 1 file");
  } else if (files_read <= 5) {
    r.score = relevant_to_goal ? 0.75 : 0.55;
    r.reason = std::to_string(files_read) + " files read";
    r.evidence.push_back("read " + std::to_string(files_read) + " files");
  } else {
    r.score = relevant_to_goal ? 0.85 : 0.70;
    r.reason = std::to_string(files_read) + " files read";
    r.evidence.push_back("read " + std::to_string(files_read) + " files");
  }

  return r;
}

// ---------------------------------------------------------------------------
// Build confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_build(bool build_passed,
                                                 const std::string &error_summary) {
  ConfidenceResult r;

  if (build_passed) {
    r.score = 0.95;
    r.reason = "build succeeded";
    r.evidence.push_back("build passed");
  } else {
    r.score = 0.25;
    r.reason = "build failed";
    r.gaps.push_back("build errors: " + error_summary);
    r.evidence.push_back("build failed");
  }

  return r;
}

// ---------------------------------------------------------------------------
// Test confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_tests(int tests_run,
                                                 int tests_passed,
                                                 int tests_failed) {
  ConfidenceResult r;

  if (tests_run == 0) {
    r.score = 0.30;
    r.reason = "no tests run";
    r.gaps.push_back("no tests available");
    return r;
  }

  double pass_rate = static_cast<double>(tests_passed) / tests_run;

  if (tests_failed == 0) {
    r.score = 0.95;
    r.reason = "all " + std::to_string(tests_run) + " tests passed";
    r.evidence.push_back(std::to_string(tests_run) + " tests passed");
  } else if (pass_rate >= 0.8) {
    r.score = 0.65;
    r.reason = std::to_string(tests_passed) + "/" +
               std::to_string(tests_run) + " tests passed";
    r.evidence.push_back(std::to_string(tests_passed) + " passed");
    r.gaps.push_back(std::to_string(tests_failed) + " failed");
  } else {
    r.score = 0.20;
    r.reason = std::to_string(tests_passed) + "/" +
               std::to_string(tests_run) + " tests passed";
    r.gaps.push_back(std::to_string(tests_failed) + " tests failed");
  }

  return r;
}

// ---------------------------------------------------------------------------
// CI confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_ci(bool logs_available,
                                              bool workflow_found,
                                              bool failure_identified) {
  ConfidenceResult r;
  int factors = 0;
  int total = 3;

  if (logs_available) {
    factors++;
    r.evidence.push_back("logs available");
  } else {
    r.gaps.push_back("logs unavailable");
  }

  if (workflow_found) {
    factors++;
    r.evidence.push_back("workflow file found");
  } else {
    r.gaps.push_back("workflow file not found");
  }

  if (failure_identified) {
    factors++;
    r.evidence.push_back("failure identified");
  } else {
    r.gaps.push_back("failure not identified");
  }

  r.score = static_cast<double>(factors) / total;

  if (factors == total) {
    r.reason = "all CI evidence available";
  } else if (factors >= 2) {
    r.reason = "partial CI evidence (" + std::to_string(factors) +
               "/" + std::to_string(total) + ")";
  } else {
    r.reason = "insufficient CI evidence (" + std::to_string(factors) +
               "/" + std::to_string(total) + ")";
  }

  return r;
}

// ---------------------------------------------------------------------------
// Discovery confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_discovery(int source_files,
                                                     int services_found,
                                                     bool has_tests,
                                                     bool has_ci) {
  ConfidenceResult r;
  int factors = 0;
  int total = 4;

  if (source_files > 0) {
    factors++;
    r.evidence.push_back(std::to_string(source_files) + " source files found");
  } else {
    r.gaps.push_back("no source files found");
  }

  if (services_found > 0) {
    factors++;
    r.evidence.push_back(std::to_string(services_found) + " services found");
  } else {
    r.gaps.push_back("no services found");
  }

  if (has_tests) {
    factors++;
    r.evidence.push_back("tests available");
  } else {
    r.gaps.push_back("no tests found");
  }

  if (has_ci) {
    factors++;
    r.evidence.push_back("CI configured");
  } else {
    r.gaps.push_back("no CI found");
  }

  r.score = static_cast<double>(factors) / total;

  if (factors == total) {
    r.reason = "full project discovery";
  } else if (factors >= 2) {
    r.reason = "partial discovery (" + std::to_string(factors) +
               "/" + std::to_string(total) + ")";
  } else {
    r.reason = "limited discovery (" + std::to_string(factors) +
               "/" + std::to_string(total) + ")";
  }

  return r;
}

// ---------------------------------------------------------------------------
// Combine multiple results
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::combine(
    const std::vector<ConfidenceResult> &results) {

  ConfidenceResult r;

  if (results.empty()) {
    r.score = 0.0;
    r.reason = "no evidence";
    r.gaps.push_back("no stages evaluated");
    return r;
  }

  double total = 0.0;
  for (auto &cr : results) {
    total += cr.score;
    r.evidence.insert(r.evidence.end(),
                      cr.evidence.begin(), cr.evidence.end());
    r.gaps.insert(r.gaps.end(),
                  cr.gaps.begin(), cr.gaps.end());
  }

  r.score = total / results.size();

  if (r.score >= 0.8) {
    r.reason = "high confidence (" + fmt_score(r.score) + ")";
  } else if (r.score >= 0.5) {
    r.reason = "moderate confidence (" + fmt_score(r.score) + ")";
  } else if (r.score >= 0.3) {
    r.reason = "low confidence (" + fmt_score(r.score) + ")";
  } else {
    r.reason = "very low confidence (" + fmt_score(r.score) + ")";
  }

  return r;
}

// ---------------------------------------------------------------------------
// Decision helpers
// ---------------------------------------------------------------------------

bool ConfidenceService::should_proceed(const ConfidenceResult &confidence,
                                        double threshold) {
  return confidence.score >= threshold;
}

bool ConfidenceService::should_stop(const ConfidenceResult &confidence,
                                     double threshold) {
  return confidence.score < threshold;
}

} // namespace Services
