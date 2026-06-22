#pragma once
#include <array>
#include <cstdio>
#include <string>

namespace Services {

struct StreamTelemetry {
  std::string command;
  long long started_at{0};
  long long first_output_at{0};
  long long last_output_at{0};
  long long completed_at{0};
  int lines_streamed{0};
  int exit_code{0};
  bool timed_out{false};
};

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

  // Execute with streaming telemetry recording
  static std::string execute_with_telemetry(const std::string &command,
                                            StreamTelemetry &telemetry,
                                            int timeout_seconds = 30,
                                            size_t max_bytes = 100 * 1024);
};
} // namespace Services
