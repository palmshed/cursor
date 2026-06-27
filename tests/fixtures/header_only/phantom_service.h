// Header-only declaration — class declared here, no .cpp exists
// Tests planner recovery: finds header, realizes no implementation, seeks elsewhere
#pragma once
#include <string>

namespace Services {
class PhantomService {
public:
  std::string execute(const std::string &cmd);
  int query_count{0};
};
}
