#pragma once
#include "services/execution_engine.h"
#include <string>
#include <vector>

namespace Services {

// ---------------------------------------------------------------------------
// EvidenceGap -- exhaustive description of planner state.
//
// The gap fully describes what evidence is still needed. The planner never
// inspects EvidenceStore directly -- it reads the gap and decides what to
// resolve.
//
// EvidenceGapEngine knows nothing about tools, GoalType, or planner logic.
// It is a pure domain function: given requirements + evidence, produce a gap.
// ---------------------------------------------------------------------------

struct EvidenceGap {
  struct RequirementStatus {
    EvidenceRequirement requirement;
    EvidenceQuality best_quality{QualNone};
    bool is_independently_verified{false};

    // No evidence of this type exists at all.
    bool missing() const {
      return best_quality == QualNone;
    }

    // Evidence exists but is below the minimum required quality.
    bool weak() const {
      return best_quality != QualNone && best_quality < requirement.min_quality;
    }

    // Evidence meets quality conditions (quality exists and >= minimum).
    // This does NOT check independent verification.
    bool satisfied() const {
      if (best_quality == QualNone)
        return false;
      if (best_quality < requirement.min_quality)
        return false;
      return true;
    }

    // Quality conditions are satisfied but evidence is not independently
    // verified. When require_independent_verification is true, this is a
    // planner gap. When false, it is an improvement opportunity.
    bool unverified() const {
      if (best_quality == QualNone)
        return false;
      if (best_quality < requirement.min_quality)
        return false;
      return !is_independently_verified;
    }

    // All conditions met: quality conditions + optional verification.
    bool complete() const {
      return satisfied() && (!requirement.require_independent_verification || is_independently_verified);
    }

    // Priority for planner: missing > weak > unverified (when required) > 0 (satisfied).
    // Higher number = higher priority.
    int priority() const {
      if (missing()) return 4;
      if (weak())   return 3;
      if (unverified() && requirement.require_independent_verification) return 2;
      return 0; // no gap
    }
  };

  std::vector<RequirementStatus> requirements;

  // All requirements are satisfied at their minimum quality.
  bool complete() const {
    for (auto &rs : requirements)
      if (!rs.complete())
        return false;
    return true;
  }

  bool has_missing() const {
    for (auto &rs : requirements)
      if (rs.missing()) return true;
    return false;
  }

  bool has_weak() const {
    for (auto &rs : requirements)
      if (rs.weak()) return true;
    return false;
  }

  bool has_unverified() const {
    for (auto &rs : requirements)
      if (rs.unverified()) return true;
    return false;
  }

  // Index of the highest-priority unresolved requirement, or -1 if all
  // satisfied. Planner uses this to decide what to resolve next.
  int highest_priority_index() const {
    int best = -1, best_pri = 0;
    for (int i = 0; i < (int)requirements.size(); i++) {
      int p = requirements[i].priority();
      if (p > best_pri) { best_pri = p; best = i; }
    }
    return best;
  }

  const RequirementStatus *highest_priority_item() const {
    int idx = highest_priority_index();
    return idx >= 0 ? &requirements[idx] : nullptr;
  }

  // Gap metrics for telemetry — how many of each gap type.
  int missing_count() const {
    int n = 0;
    for (auto &rs : requirements) if (rs.missing()) n++;
    return n;
  }

  int weak_count() const {
    int n = 0;
    for (auto &rs : requirements) if (rs.weak()) n++;
    return n;
  }

  int unverified_count() const {
    int n = 0;
    for (auto &rs : requirements) if (rs.unverified()) n++;
    return n;
  }

  int satisfied_count() const {
    int n = 0;
    for (auto &rs : requirements) if (rs.satisfied()) n++;
    return n;
  }
};

// ---------------------------------------------------------------------------
// EvidenceGapEngine -- deterministic gap evaluator.
//
// Same inputs always produce the same gap. No side effects. No tool
// knowledge. No planner state.
// ---------------------------------------------------------------------------

class EvidenceGapEngine {
public:
  // Evaluate the gap between requirements and collected evidence.
  // Deterministic: same requirements + same evidence → same gap.
  EvidenceGap evaluate(
      const std::vector<EvidenceRequirement> &requirements,
      const EvidenceStore &evidence) const;
};

} // namespace Services
