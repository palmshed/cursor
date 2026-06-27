#include "services/tool_resolver.h"

namespace Services {

// ---------------------------------------------------------------------------
// Mapping table: (evidence_class, action) → (tool, args_template).
//
// The {term} placeholder is replaced with the search term at resolve time.
// This is intentionally a flat table -- changing tool strategies means
// editing rows, not planner code.
// ---------------------------------------------------------------------------

namespace {

struct Mapping {
  EvidenceClass ec;
  PlannerAction action;
  const char *tool;
  const char *args_template; // "{term}" gets replaced with search_term
};

static const Mapping TABLE[] = {
  // FileSearch: find is primary, grep is verify
  {FileSearch,  PlannerAction::Acquire,    "find",  "{term}"},
  {FileSearch,  PlannerAction::Strengthen, "find",  "{term} --impl"},
  {FileSearch,  PlannerAction::Verify,     "grep",  "{term}"},

  // FileContent: read is the only content tool
  {FileContent, PlannerAction::Acquire,    "read",  "{term}"},
  {FileContent, PlannerAction::Strengthen, "read",  "{term}"},
  {FileContent, PlannerAction::Verify,     "read",  "{term}"},

  // Discovery: codebase overview
  {Discovery,   PlannerAction::Acquire,    "discovery", ""},
  {Discovery,   PlannerAction::Strengthen, "discovery", "--deep"},
  {Discovery,   PlannerAction::Verify,     "discovery", ""},

  // GitLog: git history
  {GitLog,      PlannerAction::Acquire,    "git",   "log --oneline -5"},
  {GitLog,      PlannerAction::Strengthen, "git",   "log --oneline -20"},
  {GitLog,      PlannerAction::Verify,     "git",   "log --oneline -5"},

  // Build: cmake
  {Build,       PlannerAction::Acquire,    "cmake", "--build"},
  {Build,       PlannerAction::Strengthen, "cmake", "--build --verbose"},
  {Build,       PlannerAction::Verify,     "cmake", "--build"},

  // Test: ctest
  {Test,        PlannerAction::Acquire,    "ctest", ""},
  {Test,        PlannerAction::Strengthen, "ctest", "--output-on-failure"},
  {Test,        PlannerAction::Verify,     "ctest", ""},

  // CIWorkflow: gh
  {CIWorkflow,  PlannerAction::Acquire,    "gh",    "run view"},
  {CIWorkflow,  PlannerAction::Strengthen, "gh",    "run view --log"},
  {CIWorkflow,  PlannerAction::Verify,     "gh",    "run view"},
};

constexpr size_t TABLE_SIZE = sizeof(TABLE) / sizeof(TABLE[0]);

} // anonymous namespace

ToolRequest ToolResolver::resolve(
    const PlannerDecision &decision,
    const std::string &search_term) const {

  if (!decision.has_work)
    return {};

  for (size_t i = 0; i < TABLE_SIZE; i++) {
    auto &m = TABLE[i];
    if (m.ec == decision.evidence_class && m.action == decision.action) {
      std::string args = m.args_template;
      // Replace {term} placeholder with actual search term
      size_t pos = args.find("{term}");
      if (pos != std::string::npos)
        args.replace(pos, 6, search_term);
      return {m.tool, args};
    }
  }

  // No mapping found — safe empty request
  return {};
}

} // namespace Services
