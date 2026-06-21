#pragma once
#include "core/metrics.h"
#include <string>
#include <vector>

namespace Services {

struct DashboardOutcomeAggregate {
  int total_sessions{0};
  int total_events{0};

  // Outcome counts
  int success_count{0};
  int failure_count{0};
  int insufficient_evidence_count{0};
  int user_rejected_count{0};

  // Trust metrics (events where set)
  int trust_events{0};
  int plan_approved_count{0};
  int diff_approved_count{0};
  int user_corrected_goal_count{0};
  int reverted_count{0};

  // Recovery metrics (events where set)
  int recovery_events{0};
  long long total_attempts{0};
  double total_confidence_delta{0.0};
  int evidence_found_count{0};
  int verification_found_count{0};

  // Confidence bands (for calibration)
  // band[0..4] = 0.0-0.2, 0.2-0.4, 0.4-0.6, 0.6-0.8, 0.8-1.0
  int band_cb[5]{0,0,0,0,0};  // events per confidence_before band
  int band_success_cb[5]{0,0,0,0,0};  // successes per band
  int band_ca[5]{0,0,0,0,0};  // events per confidence_after band
  int band_success_ca[5]{0,0,0,0,0};  // successes per band

  // Interactive vs benchmark breakdown
  int interactive_events{0};
  int benchmark_events{0};
  int band_cb_interactive[5]{0,0,0,0,0};
  int band_success_cb_interactive[5]{0,0,0,0,0};
  int band_cb_benchmark[5]{0,0,0,0,0};
  int band_success_cb_benchmark[5]{0,0,0,0,0};

  // Drill-down: session IDs matching filter
  std::vector<std::string> matching_sessions;

  // Computed percentages
  double success_pct() const;
  double failure_pct() const;
  double insufficient_evidence_pct() const;
  double user_rejected_pct() const;
  double user_corrected_goal_pct() const;
  double diff_rejected_pct() const;
  double avg_attempts() const;
  double avg_confidence_delta() const;
};

class DashboardService {
public:
  // Generate dashboard from all replay sessions.
  // filter: empty = aggregate only, "outcome=<name>" = drill-down
  static DashboardOutcomeAggregate generate(const std::string &filter = "");
};

} // namespace Services
