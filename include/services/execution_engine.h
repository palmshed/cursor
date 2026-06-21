#pragma once
#include "core/metrics.h"
#include <functional>
#include <string>
#include <vector>

namespace Core {
class UIManager;
}

namespace Services {

struct ToolCall {
  std::string tool;
  std::string args;
};

struct EvidenceStore {
  std::vector<std::string> facts;
  std::vector<std::string> modified_files;
  std::string build_result;
  std::string test_result;

  void add_fact(const std::string &fact);
  bool has_fact_containing(const std::string &keyword) const;
};

struct ExecutionResult {
  bool success{false};
  std::string summary;
  std::string ai_response;
  EvidenceStore evidence;
  int goal_type{0};
  double confidence{0.0};
  bool stopped_early{false};
  std::string stop_reason;
  Core::Outcome outcome{Core::Outcome::InsufficientEvidence};
  Core::RecoveryMetrics recovery_metrics;
  Core::TrustMetrics trust_metrics;
};

class ExecutionEngine {
public:
  using ToolRunner = std::function<std::string(const ToolCall &)>;

  enum GoalType { GeneralChat, CodebaseQuery, CodeChange, CICheck, GitHubInvestigation };

  ExecutionEngine() = default;

  // Main entry — runs the evidence loop then returns
  ExecutionResult execute(const std::string &goal, ToolRunner run_tool,
                          Core::UIManager &ui);

  // Completion check — public so callers can inspect
  bool goal_is_achieved(const std::string &goal,
                        const EvidenceStore &evidence);

  static std::string goal_type_name(GoalType t);

private:
  GoalType classify_goal(const std::string &goal);

  ToolCall select_next_tool(const std::string &goal, GoalType type,
                            const EvidenceStore &evidence);
  bool check_completion(const std::string &goal, GoalType type,
                        const EvidenceStore &evidence);
};

} // namespace Services
