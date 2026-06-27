// Similar symbol -- exists alongside real ExecutionResult to test disambiguation
// The planner should find real ExecutionResult, not this one
#pragma once
#include <string>
#include <vector>

struct ExecutionResult {
  bool ok{false};
  std::vector<std::string> messages;
};
