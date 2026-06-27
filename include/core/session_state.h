#pragma once
#include "core/investigation_session.h"
#include "core/model_catalog.h"
#include "core/metrics.h"
#include <optional>
#include <string>

namespace Core {

enum class PermissionMode { REVIEW, APPLY, AGENT };

struct SessionState {
  // Active model selection (replaces the old AgentMode + ollama_model_ pair).
  ModelConfig active_model{
      "ollama-llama3.2-3b",
      "Llama 3.2 3B",
      Provider::Ollama,
      "llama3.2:3b",
      {},
      CostTier::Local,
      ModelCategory::General,
      /*recommended=*/false,
  };

  // Runtime flags
  bool verbose_mode_{false};
  bool inspect_mode_{false};
  bool llm_classifier_{false};
  PermissionMode perm_mode_{PermissionMode::APPLY};

  // Counters
  int       command_count_{0};
  long long token_usage_{0};

  // Confidence / outcome telemetry
  double last_confidence_before{0.0};
  double last_confidence_after{0.0};
  Outcome       last_outcome{Outcome::InsufficientEvidence};
  ExecutionPath last_execution_path{ExecutionPath::Unknown};
  RecoveryMetrics last_recovery_metrics{};
  TrustMetrics    last_trust_metrics{};

  // Repository context (gathered once at startup)
  std::string cwd_;
  std::string repo_root_;
  std::string git_branch_;
  std::string git_status_;
  std::string build_artifacts_;

  // Last investigation session (planner artifact for the most recent query)
  std::optional<InvestigationSession> last_investigation;
};

} // namespace Core
