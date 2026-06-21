#pragma once
#include <string>
#include <vector>

namespace Services {

struct Capability {
  std::string name;
  bool available;
  std::string description;
};

class CapabilityRegistry {
public:
  static std::vector<Capability> list_all();

private:
  static bool check_tool(const std::string &name);
  static bool check_git_remote();
  static bool check_git_diff();
  static bool check_git_status();
  static bool check_docker();
  static bool check_brew();
};

} // namespace Services
