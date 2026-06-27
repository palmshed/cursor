#pragma once
#include "core/metrics.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace Services { struct ExecutionResult; }

namespace Core {

// -----------------------------------------------------------------------------
// ToolInvocation — a planner-facing record of a tool being used
// (not a tool log: no exit codes, no raw output, no stderr)
// -----------------------------------------------------------------------------
struct ToolInvocation {
  std::string tool;     // "grep", "read", "git", "find", "discovery"
  std::string query;    // what the planner was looking for (args in intent terms)
  std::string result;   // human-readable: "10 commits", "3 matches in 2 files"
};

// -----------------------------------------------------------------------------
// SymbolReference — a resolved symbol in the codebase
// -----------------------------------------------------------------------------
struct SymbolReference {
  std::filesystem::path file;
  std::string symbol;   // class / function / variable name, or empty for file refs
};

// -----------------------------------------------------------------------------
// InvestigationSession — the single canonical record of one investigation
//
// This is a planner artifact, not a UI object.
// Every other representation (UI output, JSON, replay, /inspect, telemetry)
// is a view over this struct.
// -----------------------------------------------------------------------------
struct InvestigationSession {
  std::string goal;                              // original user question
  std::string conclusion;                        // what the planner determined
  double confidence{0.0};                        // 0.0 – 1.0
  std::chrono::milliseconds duration{0};         // wall-clock investigation time
  Core::Outcome outcome{Core::Outcome::InsufficientEvidence};

  std::vector<ToolInvocation> tools_used;
  std::vector<std::filesystem::path> files_examined;
  std::vector<SymbolReference> symbols_found;
  std::vector<std::string> reasoning_steps;      // why each tool was chosen
  std::vector<std::string> evidence_summary;     // what was found

  bool sufficient_evidence{false};
  bool investigation_complete{false};

  // Build a session from an ExecutionResult (defined in execution_engine.cpp)
  static InvestigationSession from_result(
      const Services::ExecutionResult &result,
      std::string goal,
      std::chrono::milliseconds duration);
};

} // namespace Core
