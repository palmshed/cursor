#include "services/planner.h"

namespace Services {

std::string PlannerDecision::describe() const {
  if (!has_work) return "all satisfied";
  const char *action_str = "";
  switch (action) {
    case PlannerAction::Acquire:    action_str = "acquire"; break;
    case PlannerAction::Strengthen: action_str = "strengthen"; break;
    case PlannerAction::Verify:     action_str = "verify"; break;
  }
  return std::string(action_str) + " " + std::to_string(static_cast<int>(evidence_class));
}

PlannerDecision Planner::decide(const EvidenceGap &gap) const {
  PlannerDecision d;

  auto *item = gap.highest_priority_item();
  if (!item) {
    d.has_work = false;
    return d;
  }

  d.has_work = true;
  d.evidence_class = item->requirement.ec;

  if (item->missing()) {
    d.action = PlannerAction::Acquire;
  } else if (item->weak()) {
    d.action = PlannerAction::Strengthen;
  } else {
    d.action = PlannerAction::Verify;
  }

  return d;
}

} // namespace Services
