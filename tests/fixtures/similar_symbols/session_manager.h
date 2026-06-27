// Similar symbol -- exists alongside real SessionState to test disambiguation
// The planner should find real SessionState, not this one
#pragma once
#include <string>

struct SessionState {
  std::string name;
  int version{0};
};
