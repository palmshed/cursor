#pragma once
#include <string>
#include <vector>

namespace Services {

struct CheckResult {
  std::string name;
  bool passed;
  std::string details;
  std::string fix_suggestion;
};

class VerificationService {
public:
  static std::vector<CheckResult> run_all_checks();
  static CheckResult check_file_read_write();
  static CheckResult check_grep();
  static CheckResult check_git();
  static CheckResult check_gh();
  static CheckResult check_npm();
  static CheckResult check_node();
  static CheckResult check_python();
  static CheckResult check_curl();
  static CheckResult check_cmake();
  static CheckResult check_make();
  static CheckResult check_ollama();
  static CheckResult check_network();
  static CheckResult check_replay_dir();
  static CheckResult check_github_auth();
  static CheckResult check_npm_auth();

private:
  static std::string run_command(const std::string &cmd);
  static std::string trim_output(const std::string &s);
};

} // namespace Services
