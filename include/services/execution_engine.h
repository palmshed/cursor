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

struct ToolResult {
  std::string tool;
  std::string args;
  std::string out;
  std::string err;
  int exit_code{0};

  bool success() const { return exit_code == 0; }
};

enum EvidenceClass : int { FileSearch, FileContent, GitLog, Build, Test, Discovery, CIWorkflow };
enum EvidenceNeed : int { Default, CommitHistory };
enum class ClassifierMode : int { Deterministic, LLM };

struct EvidenceStore {
  std::vector<std::string> facts;
  std::vector<std::string> modified_files;
  std::string build_result;
  std::string test_result;
  std::vector<EvidenceClass> classes;

  void add_fact(const std::string &fact);
  bool has_fact_containing(const std::string &keyword) const;
  void mark_evidence_class(EvidenceClass ec);
  bool has_any_evidence_class(const std::vector<EvidenceClass> &required) const;
  bool has_all_evidence_classes(const std::vector<EvidenceClass> &required) const;
};

struct ExecutionResult {
  bool success{false};
  std::string summary;
  std::string ai_response;
  EvidenceStore evidence;
  std::vector<ToolResult> tool_history;
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
  using ToolRunner = std::function<ToolResult(const ToolCall &)>;

  enum GoalType { GeneralChat, SessionState, CommitHistory, CodebaseQuery, CodebaseOverview, CodeChange, CICheck, GitHubInvestigation, ArchitectureReview };

  ExecutionEngine() = default;

  void set_ai_service(class AIService *ai) { ai_ = ai; }
  void set_classifier_mode(ClassifierMode mode) { mode_ = mode; }

  // Main entry — runs the evidence loop then returns
  ExecutionResult execute(const std::string &goal, ToolRunner run_tool,
                          Core::UIManager &ui);

  // Completion check — public so callers can inspect
  bool goal_is_achieved(const std::string &goal,
                        const EvidenceStore &evidence);

  static std::string goal_type_name(GoalType t);

private:
  ClassifierMode mode_{ClassifierMode::Deterministic};
  class AIService *ai_{nullptr};

  GoalType classify_goal(const std::string &goal);
  GoalType classify_goal_llm(const std::string &goal);
  ToolCall select_next_tool(const std::string &goal, GoalType type,
                            const EvidenceStore &evidence,
                            const std::vector<ToolResult> &tool_history = {});
  ToolCall select_next_tool_llm(const std::string &goal, GoalType type,
                                const EvidenceStore &evidence,
                                const std::vector<ToolResult> &tool_history = {});
  bool check_completion(const std::string &goal, GoalType type,
                        const EvidenceStore &evidence);
  static EvidenceNeed detect_evidence_need(const std::string &goal);
  static std::vector<EvidenceClass> required_evidence(const std::string &goal, GoalType type);
  std::string build_review_report(const std::vector<ToolResult> &tool_history) const;
};

} // namespace Services
