#pragma once
#include <string>

namespace Core {

enum class ExecutionPath {
  Unknown,
  ChatOnly,
  Engine,
  TaskPipeline,
  DirectService,
  MetaCommand,
  ShellEscape
};

inline const char *execution_path_name(ExecutionPath p) {
  switch (p) {
    case ExecutionPath::Unknown: return "unknown";
    case ExecutionPath::ChatOnly: return "chat_only";
    case ExecutionPath::Engine: return "engine";
    case ExecutionPath::TaskPipeline: return "task_pipeline";
    case ExecutionPath::DirectService: return "direct_service";
    case ExecutionPath::MetaCommand: return "meta_command";
    case ExecutionPath::ShellEscape: return "shell_escape";
  }
  return "unknown";
}

inline ExecutionPath execution_path_from_name(const std::string &n) {
  if (n == "chat_only") return ExecutionPath::ChatOnly;
  if (n == "engine") return ExecutionPath::Engine;
  if (n == "task_pipeline") return ExecutionPath::TaskPipeline;
  if (n == "direct_service") return ExecutionPath::DirectService;
  if (n == "meta_command") return ExecutionPath::MetaCommand;
  if (n == "shell_escape") return ExecutionPath::ShellEscape;
  return ExecutionPath::Unknown;
}

enum class Outcome {
  Success,
  Failure,
  InsufficientEvidence,
  UserRejected
};

inline const char *outcome_name(Outcome o) {
  switch (o) {
    case Outcome::Success: return "success";
    case Outcome::Failure: return "failure";
    case Outcome::InsufficientEvidence: return "insufficient_evidence";
    case Outcome::UserRejected: return "user_rejected";
  }
  return "unknown";
}

inline Outcome outcome_from_name(const std::string &n) {
  if (n == "success") return Outcome::Success;
  if (n == "failure") return Outcome::Failure;
  if (n == "insufficient_evidence") return Outcome::InsufficientEvidence;
  if (n == "user_rejected") return Outcome::UserRejected;
  return Outcome::InsufficientEvidence;
}

struct RecoveryMetrics {
  int attempts{0};
  int strategy_changes{0};
  bool evidence_found{false};
  bool verification_found{false};
  double confidence_delta{0.0};
  int grep_attempts{0};
  int grep_success{0};
  int grep_zero_hit{0};
  int read_attempts{0};
  int read_success{0};

  bool operator==(const RecoveryMetrics &o) const {
    return attempts == o.attempts &&
           strategy_changes == o.strategy_changes &&
           evidence_found == o.evidence_found &&
           verification_found == o.verification_found &&
           confidence_delta == o.confidence_delta &&
           grep_attempts == o.grep_attempts &&
           grep_success == o.grep_success &&
           grep_zero_hit == o.grep_zero_hit &&
           read_attempts == o.read_attempts &&
           read_success == o.read_success;
  }

  bool operator!=(const RecoveryMetrics &o) const { return !(*this == o); }
};

struct TrustMetrics {
  bool plan_approved{false};
  bool diff_approved{false};
  bool user_corrected_goal{false};
  bool reverted{false};

  bool operator==(const TrustMetrics &o) const {
    return plan_approved == o.plan_approved &&
           diff_approved == o.diff_approved &&
           user_corrected_goal == o.user_corrected_goal &&
           reverted == o.reverted;
  }

  bool operator!=(const TrustMetrics &o) const { return !(*this == o); }
};

} // namespace Core
