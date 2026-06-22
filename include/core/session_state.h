#pragma once
#include "agent_mode.h"
#include "metrics.h"
#include <string>

namespace Core {

enum class PermissionMode { REVIEW, APPLY, AGENT };

struct SessionState {
  AgentMode mode_{AgentMode::MODE_UNSET};
  std::string ollama_model_;
  bool verbose_mode_{false};
  bool inspect_mode_{false};
  bool llm_classifier_{false};
  PermissionMode perm_mode_{PermissionMode::APPLY};
  int command_count_{0};
  long long token_usage_{0};
  double last_confidence_before{0.0};
  double last_confidence_after{0.0};
  Outcome last_outcome{Outcome::InsufficientEvidence};
  ExecutionPath last_execution_path{ExecutionPath::Unknown};
  RecoveryMetrics last_recovery_metrics{};
  TrustMetrics last_trust_metrics{};
  // Repository context (gathered once at startup)
  std::string cwd_;
  std::string repo_root_;
  std::string git_branch_;
  std::string git_status_;
  std::string build_artifacts_;
};

} // namespace Core
