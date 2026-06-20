#pragma once
#include "agent_mode.h"
#include <string>

namespace Core {

struct SessionState {
  AgentMode mode_{AgentMode::MODE_UNSET};
  std::string ollama_model_;
  bool verbose_mode_{false};
  int command_count_{0};
  long long token_usage_{0};
};

} // namespace Core
