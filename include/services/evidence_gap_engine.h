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

    // Evidence meets quality but is not independently verified.
    // Only meaningful when satisfied() is true.
    bool unverified() const {
      return satisfied() && !is_independently_verified;
    }

    // Evidence meets all conditions: quality threshold met and, if required,
    // independently verified.
    bool satisfied() const {
      if (best_quality == QualNone)
        return false;
      if (best_quality < requirement.min_quality)
        return false;
      if (requirement.require_independent_verification && !is_independently_verified)
        return false;
      return true;
    }
  };

  std::vector<RequirementStatus> requirements;

  // All requirements are satisfied at their minimum quality.
  bool complete() const {
    for (auto &rs : requirements)
      if (!rs.satisfied())
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
