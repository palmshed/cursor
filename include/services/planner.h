#pragma once
#include "services/evidence_gap_engine.h"
#include <string>
#include <vector>

namespace Services {

// ---------------------------------------------------------------------------
// PlannerAction -- what the planner decides to do about a gap.
//
// Acquire:   no evidence exists → produce some (highest priority)
// Strengthen: evidence exists but below quality → improve it
// Verify:    quality met but no independent corroboration → get a second
//            source (lowest priority)
// ---------------------------------------------------------------------------
enum class PlannerAction {
  Acquire,
  Strengthen,
  Verify
};

// ---------------------------------------------------------------------------
// PlannerDecision -- a single decision from the planner.
//
// The planner never produces tool names, search terms, or execution plans.
// It simply identifies which gap to resolve and with what intent.
// Phase 4.3 (Tool Resolver) maps intent + evidence class → concrete tools.
// ---------------------------------------------------------------------------
struct PlannerDecision {
  bool has_work{false};
  PlannerAction action{PlannerAction::Acquire};
  EvidenceClass evidence_class{FileSearch};

  // Human-readable summary for logging/diagnostics.
  std::string describe() const;
};

// ---------------------------------------------------------------------------
// Planner -- pure domain component for evidence gap resolution.
//
// Consumes:
//   EvidenceGap (from EvidenceGapEngine)
//
// Produces:
//   PlannerDecision — which gap to resolve next, and how
//
// Invariant:
//   The Planner never inspects EvidenceStore. It reasons about gaps, not
//   raw evidence. It never references tools, GoalType, or execution state.
// ---------------------------------------------------------------------------
class Planner {
public:
  // Decide what to do next given the current gap.
  // Deterministic: same gap → same decision.
  PlannerDecision decide(const EvidenceGap &gap) const;
};

} // namespace Services
