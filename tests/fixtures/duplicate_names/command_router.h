// Duplicate filename — exists alongside real include/app/command_router.h
// Tests that the planner picks the correct one
#pragma once
#include <string>

namespace Core {
class CommandRouter {
public:
  std::string process(const std::string &input);
};
}
