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
  PermissionMode perm_mode_{PermissionMode::APPLY};
  int command_count_{0};
  long long token_usage_{0};
  double last_confidence_before{0.0};
  double last_confidence_after{0.0};
  Outcome last_outcome{Outcome::InsufficientEvidence};
  RecoveryMetrics last_recovery_metrics{};
  TrustMetrics last_trust_metrics{};
};

} // namespace Core
