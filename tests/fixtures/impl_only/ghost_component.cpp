// Implementation-only — class definition in .cpp, no header exists
// Tests planner recovery: finds .cpp, realizes no header, seeks declaration
#include <string>
#include <iostream>

namespace Services {
class GhostComponent {
public:
  std::string name;
  void activate() { std::cout << "GhostComponent activated\n"; }
};
}
