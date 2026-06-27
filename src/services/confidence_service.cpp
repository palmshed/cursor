#include "services/confidence_service.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <map>
#include <sstream>
#include <unordered_map>

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
// Category weights
// ---------------------------------------------------------------------------

const std::unordered_map<std::string, double> &ConfidenceService::category_weights() {
  // Weights sum to 1.0.
  // read is weighted highest because file content is the most informative step.
  // search is next — locating evidence is necessary but not sufficient alone.
  // verification confirms correctness when available.
  // discovery provides context but is not evidence for the answer.
  // git and ci are domain-specific adjuncts.
  static const std::unordered_map<std::string, double> weights = {
    {"read",         0.50},
    {"search",       0.25},
    {"verification", 0.10},
    {"discovery",    0.10},
    {"git",          0.025},
    {"ci",           0.025},
  };
  return weights;
}

// ---------------------------------------------------------------------------
// Category agreement (simplified: always 1.0)
// ---------------------------------------------------------------------------
//
// In practice, within-category evidence is complementary, not contradictory.
// Different tools in the same category (e.g., find + grep both in "search")
// provide different evidence strengths, not conflicting signals.
// Genuine contradiction is detected by lack of convergence across categories.
//
// The agreement slot is preserved for future use if telemetry shows
// within-category contradictions that max+convergence cannot capture.

static double category_agreement(const std::vector<double> &scores) {
  (void)scores;
  return 1.0;
}

// ---------------------------------------------------------------------------
// Detect convergence between independent categories
// ---------------------------------------------------------------------------

// Case-insensitive substring search
static bool contains_icase(const std::string &haystack, const std::string &needle) {
  if (needle.size() > haystack.size()) return false;
  auto it = std::search(
      haystack.begin(), haystack.end(),
      needle.begin(), needle.end(),
      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                  std::tolower(static_cast<unsigned char>(b)); });
  return it != haystack.end();
}

// Normalise a target string for convergence matching.
// Strips directory prefixes and file extensions from file paths,
// and converts underscores/hyphens to empty (so "memory_manager"
// can match "MemoryManager").
static std::string normalise_for_convergence(const std::string &raw) {
  // Take only the filename portion (strip directories)
  std::string s;
  auto slash = raw.rfind('/');
  if (slash != std::string::npos)
    s = raw.substr(slash + 1);
  else
    s = raw;

  // Strip file extension
  auto dot = s.rfind('.');
  if (dot != std::string::npos)
    s = s.substr(0, dot);

  // Remove underscores and hyphens so "memory_manager" → "memorymanager"
  s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
  s.erase(std::remove(s.begin(), s.end(), '-'), s.end());

  return s;
}

// Returns the number of independent categories that converge on common targets.
// Convergence is detected by case-insensitive substring matching after
// normalising targets (stripping paths/extensions/underscores).
// This catches common patterns like:
//   find "MemoryManager" + read "include/memory_manager.h"
//   grep "CommandRouter" + read "src/command_router.cpp"
static int convergence_count(
    const std::map<std::string, std::vector<std::string>> &category_targets) {

  // Pre-normalise all targets
  std::map<std::string, std::vector<std::string>> normalised;
  for (const auto &[cat, targets] : category_targets) {
    for (const auto &t : targets) {
      std::string n = normalise_for_convergence(t);
      if (!n.empty())
        normalised[cat].push_back(n);
    }
  }

  std::vector<std::string> cats;
  for (const auto &[cat, _] : normalised)
    cats.push_back(cat);

  int convergence_pairs = 0;
  for (size_t i = 0; i < cats.size(); ++i) {
    for (size_t j = i + 1; j < cats.size(); ++j) {
      const auto &targets_a = normalised.at(cats[i]);
      const auto &targets_b = normalised.at(cats[j]);

      bool found = false;
      for (const auto &ta : targets_a) {
        if (ta.empty()) continue;
        for (const auto &tb : targets_b) {
          if (tb.empty()) continue;
          // Case-insensitive substring match in either direction
          if (contains_icase(tb, ta) || contains_icase(ta, tb)) {
            found = true;
            break;
          }
        }
        if (found) break;
      }
      if (found) convergence_pairs++;
    }
  }

  return convergence_pairs;
}

// ---------------------------------------------------------------------------
// Search confidence
// ---------------------------------------------------------------------------

ConfidenceResult ConfidenceService::after_search(
    const std::string &query, int results_found) {

  ConfidenceResult r;
  r.category = "search";

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
  r.category = "read";

  if (files_read == 0) {
    r.score = 0.10;
    r.reason = "no files read";
    r.gaps.push_back("no files read");
  // In the category-weighted model, reading content is the most informative
  // evidence step. Scores are calibrated so a single relevant read contributes
  // meaningfully to the combined score, and multiple reads converge further.
  } else if (files_read == 1) {
    r.score = relevant_to_goal ? 0.75 : 0.40;
    r.reason = "1 file read" +
               std::string(relevant_to_goal ? " (relevant)" : " (may not be relevant)");
    r.evidence.push_back("read 1 file");
  } else if (files_read <= 5) {
    r.score = relevant_to_goal ? 0.88 : 0.65;
    r.reason = std::to_string(files_read) + " files read";
    r.evidence.push_back("read " + std::to_string(files_read) + " files");
  } else {
    r.score = relevant_to_goal ? 0.92 : 0.75;
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
  r.category = "verification";

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
  r.category = "verification";

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

  r.category = "ci";

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

  r.category = "discovery";

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
// Combine multiple results using category-weighted aggregation
// ---------------------------------------------------------------------------
//
// Algorithm:
//   1. Group results by evidence category
//   2. For each category: compute max score and agreement
//      (agreement measures whether entries in the same category contradict)
//   3. Apply category weights to effective scores
//   4. Award convergence bonus when independent categories agree on targets
//   5. Populate breakdown for calibration
//
// This replaces the old flat averaging which treated every tool step equally.

ConfidenceResult ConfidenceService::combine(
    const std::vector<ConfidenceResult> &results) {

  ConfidenceResult r;

  if (results.empty()) {
    r.score = 0.0;
    r.reason = "no evidence";
    r.gaps.push_back("no stages evaluated");
    return r;
  }

  // --- Step 1: Group by category ---
  std::map<std::string, std::vector<double>> category_scores;
  std::map<std::string, std::vector<std::string>> category_targets;

  for (auto &cr : results) {
    std::string cat = cr.category.empty() ? "tool_generic" : cr.category;
    category_scores[cat].push_back(cr.score);
    if (!cr.target.empty())
      category_targets[cat].push_back(cr.target);

    r.evidence.insert(r.evidence.end(),
                      cr.evidence.begin(), cr.evidence.end());
    r.gaps.insert(r.gaps.end(),
                  cr.gaps.begin(), cr.gaps.end());
  }

  // --- Step 2: Per-category max, agreement, effective score ---
  const auto &weights = category_weights();

  struct CatResult {
    double max_score;
    double agreement;
    double effective;
    double weight;
  };
  std::map<std::string, CatResult> cat_results;

  for (const auto &[cat, scores] : category_scores) {
    double max_s = *std::max_element(scores.begin(), scores.end());
    double agree = category_agreement(scores);
    // Effective score = max adjusted by agreement
    // Low agreement (contradictory evidence) reduces the max proportionally
    double effective = max_s * (0.5 + 0.5 * agree);

    auto wit = weights.find(cat);
    double w = (wit != weights.end()) ? wit->second : 0.05;

    cat_results[cat] = {max_s, agree, effective, w};
  }

  // --- Step 3: Weighted combination ---
  double total_weighted = 0.0;

  for (const auto &[cat, cr] : cat_results) {
    total_weighted += cr.weight * cr.effective;
  }

  double base_score = total_weighted;

  // --- Step 4: Convergence bonus ---
  // Only award when independent categories converge on the same target.
  // Bonuses are intentionally large because convergence of independent
  // evidence categories is the strongest signal of investigation quality.
  int cv = convergence_count(category_targets);
  double convergence_multiplier = 1.0;
  if (cv >= 1)
    convergence_multiplier = 1.25;   // one independent pair converges
  if (cv >= 2)
    convergence_multiplier = 1.40;   // two+ independent pairs converge

  r.score = std::min(base_score * convergence_multiplier, 1.0);

  // --- Step 5: Populate breakdown ---
  for (const auto &[cat, cr] : cat_results) {
    CategoryBreakdown bd;
    bd.category = cat;
    bd.entry_count = static_cast<int>(category_scores[cat].size());
    bd.max_score = cr.max_score;
    bd.min_score = *std::min_element(category_scores[cat].begin(),
                                      category_scores[cat].end());
    bd.agreement = cr.agreement;
    bd.effective_score = cr.effective;
    bd.weight = cr.weight;
    bd.contribution = cr.weight * cr.effective;
    r.breakdown.push_back(bd);
  }

  // --- Reason ---
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
