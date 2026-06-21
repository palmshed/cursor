#pragma once
#include <array>
#include <cstdio>
#include <string>

namespace Services {
class CommandService {
private:
  static const std::array<std::string, 9> dangerous_commands;

  // Check if a command is considered dangerous
  static bool is_dangerous_command(const std::string &command);

  // Internal method to execute a command and capture its output
  static std::string execute_command(const std::string &command,
                                     std::FILE *pipe,
                                     size_t max_bytes);

public:
  // Execute a shell command and return its output
  // Returns the command output or an error message if execution fails
  static std::string execute(const std::string &command,
                             int timeout_seconds = 30,
                             size_t max_bytes = 100 * 1024);
};
} // namespace Services
