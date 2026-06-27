#pragma once
#include "core/metrics.h"
#include "services/goal_understanding_service.h"
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

enum EvidenceQuality : int { QualNone, Weak, Moderate, Strong, Verified };
enum EvidenceStrength : int { StrNone, Low, Medium, High };

// What kind of match a tool producer observed.
// Producers emit semantics; quality/strength are derived from semantics.
enum class MatchSemantics : int {
  Unknown,
  ExactIdentifier,    // e.g. grep word-boundary match on a code symbol
  ExactPhrase,        // quoted phrase match
  ExactFilename,      // filename matched exactly
  Definition,         // definition site found in file content
  Declaration,        // declaration site found in file content
  Reference,          // reference/symbol-use site found
  Substring,          // partial/substring match
  TokenOverlap,       // some tokens matched, not all
  DirectoryContext,   // directory structure match
  RepositoryMetadata  // git log, gh run, build output, test output
};

enum EvidenceNeed : int { Default, CommitHistory };
enum class ClassifierMode : int { Deterministic, LLM };

struct EvidenceEntry {
  EvidenceClass type;
  EvidenceQuality quality{Weak};
  EvidenceStrength strength{Low};
  MatchSemantics semantics{MatchSemantics::Unknown};
  std::string tool;
  std::string query;
  std::string target;
  std::vector<std::string> sources;
  int match_count{0};
  int exact_match_count{0};
  bool phrase_match{false};
};

struct EvidenceRequirement {
  EvidenceClass ec;
  EvidenceQuality min_quality;
  // Verified is a derived relationship (≥2 independent producers agree
  // on the same target, or 1 authoritative source). Single tools cannot
  // assign Verified -- it is computed by the planner after tool selection.
  EvidenceStrength min_strength{StrNone};
  bool require_independent_verification{false};
};

struct EvidenceStore {
  std::vector<EvidenceEntry> entries;
  std::vector<std::string> facts;
  std::vector<std::string> modified_files;
  std::string build_result;
  std::string test_result;

  void add_fact(const std::string &fact);
  bool has_fact_containing(const std::string &keyword) const;

  // Evidence entry with semantic match description.
  // When semantics is not Unknown, quality and strength are derived from
  // semantics + match_count + phrase_match. Callers can override by passing
  // explicit quality/strength (derivation is skipped if semantics is Unknown).
  void add_evidence_entry(EvidenceClass ec,
                          const std::string &tool = "",
                          const std::string &query = "",
                          EvidenceQuality quality = Weak,
                          EvidenceStrength strength = Low,
                          int match_count = 0,
                          int exact_match_count = 0,
                          bool phrase_match = false,
                          MatchSemantics semantics = MatchSemantics::Unknown);

  // Legacy: adds entry with Weak quality (equivalent to old binary model).
  void mark_evidence_class(EvidenceClass ec);

  // Returns true if any entry of type ec has quality >= min_quality.
  bool has_quality(EvidenceClass ec, EvidenceQuality min_quality) const;

  // Legacy methods (derive from entries).
  bool has_any_evidence_class(const std::vector<EvidenceClass> &required) const;
  bool has_all_evidence_classes(const std::vector<EvidenceClass> &required) const;
};

struct ExecutionResult {
  bool success{false};
  std::string summary;
  std::string ai_response;
  // Clean evidence-only summary for AI consumption (no planner metadata).
  // Contains only extracted tool outputs, formatted as evidence statements.
  std::string evidence_summary;
  EvidenceStore evidence;
  std::vector<ToolResult> tool_history;
  int goal_type{0};
  // Goal Understanding phase: structured parse of user intent (telemetry only)
  ParseResult parsed_goal;
  double confidence{0.0};
  bool stopped_early{false};
  std::string stop_reason;
  Core::Outcome outcome{Core::Outcome::InsufficientEvidence};
  Core::RecoveryMetrics recovery_metrics;
  Core::TrustMetrics trust_metrics;
  Core::RetrievalMetrics retrieval_metrics;
};

class ExecutionEngine {
public:
  using ToolRunner = std::function<ToolResult(const ToolCall &)>;

  enum GoalType { GeneralChat, SessionState, CommitHistory, CodebaseQuery, CodebaseOverview, CodeChange, CICheck, GitHubInvestigation, ArchitectureReview };

  ExecutionEngine() = default;

  void set_ai_service(class AIService *ai) { ai_ = ai; }
  void set_classifier_mode(ClassifierMode mode) { mode_ = mode; }

  // Main entry -- runs the evidence loop then returns
  ExecutionResult execute(const std::string &goal, ToolRunner run_tool,
                          Core::UIManager &ui);

  // Completion check -- public so callers can inspect
  bool goal_is_achieved(const std::string &goal,
                        EvidenceStore &evidence);

  // Evidence derivation from Goal (Intent + Entity) -- public for testability.
  // GoalUnderstandingService must never know about evidence or tools -- this
  // function lives in the planner layer.
  // Returns requirements with minimum quality thresholds per evidence class.
  static std::vector<EvidenceRequirement> evidence_for_goal(const Goal &goal);

  // Goal-aware completion -- checks all evidence_for_goal() requirements
  // are satisfied at the required min_quality.
  // Replaces per-GoalType completion once Goal routing is active.
  static bool check_completion_goal(const Goal &goal, EvidenceStore &evidence);

  static std::string goal_type_name(GoalType t);

private:
  ClassifierMode mode_{ClassifierMode::Deterministic};
  class AIService *ai_{nullptr};

  GoalType classify_goal(const std::string &goal);
  GoalType classify_goal_llm(const std::string &goal);
  ToolCall select_next_tool(const std::string &goal, GoalType type,
                            EvidenceStore &evidence,
                            const std::vector<ToolResult> &tool_history = {});
  ToolCall select_next_tool_llm(const std::string &goal, GoalType type,
                                const EvidenceStore &evidence,
                                const std::vector<ToolResult> &tool_history = {});
  ToolCall select_recovery_tool(const std::string &goal, GoalType type,
                                EvidenceStore &evidence,
                                const std::vector<ToolResult> &tool_history);
  bool check_completion(const std::string &goal, GoalType type,
                        EvidenceStore &evidence);
  static EvidenceNeed detect_evidence_need(const std::string &goal);
  static std::vector<EvidenceClass> required_evidence(const std::string &goal, GoalType type);
  std::string build_review_report(const std::vector<ToolResult> &tool_history) const;
};

} // namespace Services
