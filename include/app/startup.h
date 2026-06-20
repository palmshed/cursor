#pragma once
#include "agent.h"

namespace Core {

class Startup {
public:
  void initialize(Agent &agent);
  static bool is_online_mode(const Agent &agent);
};

} // namespace Core
