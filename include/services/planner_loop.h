#pragma once
#include "services/execution_engine.h"
#include "services/planner.h"
#include "services/tool_resolver.h"
#include <string>
#include <vector>

namespace Services {

// ---------------------------------------------------------------------------
// PlannerLoopStep -- one iteration of the planner-driven investigation cycle.
//
// Captures what the planner would decide and which tool it would resolve to,
// compared with what the existing loop actually did.
// ---------------------------------------------------------------------------
struct PlannerLoopStep {
  bool planner_would_continue{false};
  PlannerDecision decision;
  ToolRequest request;
  std::string actual_tool;
  std::string actual_args;
  bool agreement{false};
  std::string reason; // why disagreed (if applicable)

  std::string describe() const;
};

// ---------------------------------------------------------------------------
// PlannerShadowMetrics -- aggregated comparison data collected across the
// entire investigation by running the planner loop in shadow mode.
// ---------------------------------------------------------------------------
struct PlannerShadowMetrics {
  std::vector<PlannerLoopStep> steps;

  int total_iterations() const { return (int)steps.size(); }
  int agreements() const;
  int disagreements() const;

  // Summary line for logging
  std::string summary() const;
};

// ---------------------------------------------------------------------------
// run_planner_shadow_step -- evaluate current gap + planner decision and
// compare with the actual tool the existing loop chose.
//
// Called after each tool execution in the existing execute() loop.
// Does not modify evidence or execute tools -- pure observation.
// ---------------------------------------------------------------------------
PlannerLoopStep run_planner_shadow_step(
    const Goal &goal,
    const EvidenceStore &evidence,
    const std::string &search_term,
    const std::string &actual_tool,
    const std::string &actual_args);

// ---------------------------------------------------------------------------
// run_planner_loop -- the planner-driven investigation cycle.
//
// Owns the cycle: Gap → Planner → Resolver → Tool → Evidence → Gap.
// Replaces the existing select_next_tool() + recovery loop.
//
// Terminates when:
//   1. All requirements satisfied
//   2. No actionable gaps remain
//   3. Budget exhausted (iterations)
//   4. Fatal tool failure
//
// The caller (execute()) owns orchestration (session setup, telemetry,
// AI synthesis, formatting).
// ---------------------------------------------------------------------------
struct PlannerLoopResult {
  bool complete{false};
  std::string stop_reason;
  PlannerShadowMetrics metrics;
};

PlannerLoopResult run_planner_loop(
    const Goal &goal,
    EvidenceStore &evidence,
    Core::UIManager &ui,
    const ExecutionEngine::ToolRunner &run_tool,
    const std::string &search_term,
    int max_iterations = 20);

} // namespace Services
