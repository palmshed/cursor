#include "services/execution_engine.h"
#include "services/planner_loop.h"
#include "ui/ui_manager.h"
#include <set>
#include <sstream>

namespace Services {

// ---------------------------------------------------------------------------
// PlannerLoopStep
// ---------------------------------------------------------------------------

std::string PlannerLoopStep::describe() const {
  if (!planner_would_continue)
    return "gap satisfied";
  std::string s = decision.describe() + "\u2192" + request.tool;
  if (!request.args.empty()) s += " " + request.args;
  s += " actual=" + actual_tool;
  if (!actual_args.empty()) s += " " + actual_args;
  s += agreement ? " agree" : " disagree(" + reason + ")";
  return s;
}

std::string PlannerLoopStep::describe_full() const {
  std::string s = describe();
  if (!planner_would_continue || agreement) return s;
  s += expected_disagreement ? " [expected]" : " [UNEXPECTED]";
  return s;
}

// ---------------------------------------------------------------------------
// PlannerShadowMetrics
// ---------------------------------------------------------------------------

int PlannerShadowMetrics::agreements() const {
  int n = 0;
  for (auto &s : steps)
    if (s.agreement) n++;
  return n;
}

int PlannerShadowMetrics::disagreements() const {
  int n = 0;
  for (auto &s : steps)
    if (s.planner_would_continue && !s.agreement) n++;
  return n;
}

int PlannerShadowMetrics::expected_disagreements() const {
  int n = 0;
  for (auto &s : steps)
    if (s.planner_would_continue && !s.agreement && s.expected_disagreement) n++;
  return n;
}

int PlannerShadowMetrics::unexpected_disagreements() const {
  int n = 0;
  for (auto &s : steps)
    if (s.planner_would_continue && !s.agreement && !s.expected_disagreement) n++;
  return n;
}

std::string PlannerShadowMetrics::summary() const {
  std::ostringstream oss;
  oss << "planner_shadow: steps=" << steps.size()
      << " agree=" << agreements()
      << " disagree=" << disagreements()
      << " expected=" << expected_disagreements()
      << " unexpected=" << unexpected_disagreements();
  return oss.str();
}

std::string PlannerShadowMetrics::detailed_report() const {
  std::ostringstream oss;
  for (size_t i = 0; i < steps.size(); i++) {
    auto &s = steps[i];
    oss << "  step[" << i << "]: " << s.describe_full() << "\n";
  }
  return oss.str();
}

// ---------------------------------------------------------------------------
// run_planner_shadow_step
// ---------------------------------------------------------------------------

PlannerLoopStep run_planner_shadow_step(
    const Goal &goal,
    const EvidenceStore &evidence,
    const std::string &search_term,
    const std::string &actual_tool,
    const std::string &actual_args) {

  PlannerLoopStep step;
  step.actual_tool = actual_tool;
  step.actual_args = actual_args;

  if (!goal.is_known()) {
    step.reason = "unknown_goal";
    step.expected_disagreement = true;
    return step;
  }

  auto reqs = ExecutionEngine::evidence_for_goal(goal);
  EvidenceGapEngine gape;
  auto gap = gape.evaluate(reqs, evidence);

  if (gap.complete()) {
    step.planner_would_continue = false;
    step.reason = "complete";
    step.expected_disagreement = true;
    return step;
  }

  step.planner_would_continue = true;

  Planner pln;
  step.decision = pln.decide(gap);

  if (step.decision.has_work) {
    ToolResolver resolver;
    step.request = resolver.resolve(step.decision, search_term);
  }

  // Compare planner's tool choice with actual
  if (step.request.empty()) {
    step.agreement = false;
    step.reason = "planner_has_no_tool";
    step.expected_disagreement = true;
  } else if (step.request.tool == actual_tool) {
    // Tools match — moderate agreement (args may differ)
    step.agreement = true;
    step.reason = "tool_match";
  } else {
    step.agreement = false;
    step.reason = std::string("tool_mismatch planner=") + step.request.tool +
                   " actual=" + actual_tool;
    // Classify expected disagreements:
    // Tools that can produce similar evidence are interchangeable.
    // A mismatch between tools in the same group is expected.
    // Tools that can produce similar evidence are interchangeable.
    // These groups capture common tool-choice variations.
    static const std::set<std::string> content_tools =
        {"find", "grep", "references", "read", "discovery"};
    static const std::set<std::string> build_tools =
        {"cmake", "ctest", "gh", "git"};
    static const std::set<std::string> review_tools =
        {"find", "grep", "references", "read", "discovery", "git"};

    bool same_group =
        (content_tools.count(step.request.tool) && content_tools.count(actual_tool)) ||
        (build_tools.count(step.request.tool) && build_tools.count(actual_tool)) ||
        (review_tools.count(step.request.tool) && review_tools.count(actual_tool));

    step.expected_disagreement = same_group;
  }

  return step;
}

// ---------------------------------------------------------------------------
// run_planner_loop
// ---------------------------------------------------------------------------

PlannerLoopResult run_planner_loop(
    const Goal &goal,
    EvidenceStore &evidence,
    Core::UIManager &ui,
    const ExecutionEngine::ToolRunner &run_tool,
    const std::string &search_term,
    int max_iterations) {

  PlannerLoopResult result;

  if (!goal.is_known()) {
    result.stop_reason = "unknown_goal";
    return result;
  }

  auto reqs = ExecutionEngine::evidence_for_goal(goal);
  EvidenceGapEngine gape;
  Planner pln;
  ToolResolver resolver;

  for (int i = 0; i < max_iterations; i++) {
    auto gap = gape.evaluate(reqs, evidence);

    // Collect metrics
    PlannerLoopStep step;
    step.planner_would_continue = !gap.complete();
    if (gap.complete()) {
      result.stop_reason = "all_satisfied";
      result.complete = true;
      result.metrics.steps.push_back(step);
      return result;
    }

    auto decision = pln.decide(gap);
    step.decision = decision;

    if (!decision.has_work) {
      // Planner says done but gap is not complete → stagnation
      result.stop_reason = "no_actionable_gaps";
      result.metrics.steps.push_back(step);
      return result;
    }

    auto request = resolver.resolve(decision, search_term);
    step.request = request;

    if (request.empty()) {
      result.stop_reason = "unresolvable_decision";
      result.metrics.steps.push_back(step);
      return result;
    }

    // Execute the tool
    ui.show_tool_invocation(request.tool, request.args);
    ToolCall tc{request.tool, request.args};
    ToolResult tr = run_tool(tc);
    tr.tool = request.tool;
    tr.args = request.args;
    // Result processing is handled by the caller via EvidenceStore
    // This is the integration point for evidence producers (Phase 4.4)

    // Record what actually happened
    step.actual_tool = request.tool;
    step.actual_args = request.args;
    step.agreement = true; // planner chose this tool
    step.reason = "executed";
    result.metrics.steps.push_back(step);
  }

  result.stop_reason = "budget_exhausted";
  return result;
}

} // namespace Services
