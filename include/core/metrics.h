#pragma once
#include <string>

namespace Core {

enum class Outcome {
  Success,
  Failure,
  InsufficientEvidence,
  UserRejected
};

inline const char *outcome_name(Outcome o) {
  switch (o) {
    case Outcome::Success: return "success";
    case Outcome::Failure: return "failure";
    case Outcome::InsufficientEvidence: return "insufficient_evidence";
    case Outcome::UserRejected: return "user_rejected";
  }
  return "unknown";
}

inline Outcome outcome_from_name(const std::string &n) {
  if (n == "success") return Outcome::Success;
  if (n == "failure") return Outcome::Failure;
  if (n == "insufficient_evidence") return Outcome::InsufficientEvidence;
  if (n == "user_rejected") return Outcome::UserRejected;
  return Outcome::InsufficientEvidence;
}

struct RecoveryMetrics {
  int attempts{0};
  int strategy_changes{0};
  bool evidence_found{false};
  bool verification_found{false};
  double confidence_delta{0.0};

  bool operator==(const RecoveryMetrics &o) const {
    return attempts == o.attempts &&
           strategy_changes == o.strategy_changes &&
           evidence_found == o.evidence_found &&
           verification_found == o.verification_found &&
           confidence_delta == o.confidence_delta;
  }

  bool operator!=(const RecoveryMetrics &o) const { return !(*this == o); }
};

struct TrustMetrics {
  bool plan_approved{false};
  bool diff_approved{false};
  bool user_corrected_goal{false};
  bool reverted{false};

  bool operator==(const TrustMetrics &o) const {
    return plan_approved == o.plan_approved &&
           diff_approved == o.diff_approved &&
           user_corrected_goal == o.user_corrected_goal &&
           reverted == o.reverted;
  }

  bool operator!=(const TrustMetrics &o) const { return !(*this == o); }
};

} // namespace Core
