#pragma once
#include "services/planner.h"

namespace Services {

// ---------------------------------------------------------------------------
// ToolRequest -- output of the Tool Resolver, input to tool execution.
//
// The resolver maps a PlannerDecision + search context → concrete tool
// invocation. It never makes planner decisions, evaluates evidence, or
// decides whether to stop.
// ---------------------------------------------------------------------------
struct ToolRequest {
  std::string tool;
  std::string args;

  bool empty() const { return tool.empty(); }
};

// ---------------------------------------------------------------------------
// ToolResolver -- table-driven mapping from planner decisions to tools.
//
// Invariant: replaceable without changing planner behavior. The table is
// the only place where evidence classes are mapped to tool names. Future
// changes (ripgrep instead of find, LSP instead of grep) modify the table,
// not the planner or the execution engine.
// ---------------------------------------------------------------------------
class ToolResolver {
public:
  // Resolve a planner decision into a concrete tool request.
  // `search_term` is the entity/term to search for (from the query).
  ToolRequest resolve(
      const PlannerDecision &decision,
      const std::string &search_term) const;
};

} // namespace Services
