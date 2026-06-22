#include "services/command_service.h"

#include "utils/platform.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <regex>
#include <stdexcept>

namespace Services {
const std::array<std::string, 9> CommandService::dangerous_commands = {
    "rm",     "sudo rm", "format", "del /", "shutdown",
    "reboot", "mkfs",    "fdisk",  "dd"};

bool CommandService::is_dangerous_command(const std::string &command) {
  // Token-based matching to avoid substring false positives
  for (const auto &danger : dangerous_commands) {
    std::regex pattern(
        "\\b" + std::regex_replace(danger, std::regex(" "), "\\s+") + "\\b");
    if (std::regex_search(command, pattern)) {
      return true;
    }
  }
  return false;
}

std::string
CommandService::execute_command([[maybe_unused]] const std::string &command,
                                FILE *pipe,
                                size_t max_bytes) {
  if (!pipe) {
    throw std::runtime_error("Failed to execute command");
  }

  std::string result;
  char buffer[256];
  size_t total_bytes = 0;

  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    total_bytes += strlen(buffer);
    if (total_bytes > max_bytes) {
      result += "\n[Output truncated]";
      break;
    }
    result += buffer;
  }

  int exit_code = Utils::Platform::close_process(pipe);
  if (exit_code != 0) {
    result += "\nExit code: " + std::to_string(exit_code);
  }

  return result.empty() ? "Command completed" : result;
}

std::string CommandService::execute(const std::string &command,
                                    int timeout_seconds,
                                    size_t max_bytes) {
  if (is_dangerous_command(command)) {
    return "Error: Dangerous command blocked";
  }

  try {
    auto future = std::async(std::launch::async, [&]() -> std::string {
      FILE *pipe = Utils::Platform::open_process(
          command + Utils::Platform::get_shell_redirect_both(), "r");
      return execute_command(command, pipe, max_bytes);
    });

    if (future.wait_for(std::chrono::seconds(timeout_seconds)) ==
        std::future_status::timeout) {
      return "Error: Command timed out";
    }
    return future.get();
  } catch (const std::exception &e) {
    return std::string("Error executing command: ") + e.what();
  }
}

std::string CommandService::execute_with_telemetry(
    const std::string &command,
    StreamTelemetry &telemetry,
    int timeout_seconds,
    size_t max_bytes) {

  telemetry.command = command;
  telemetry.started_at = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

  if (is_dangerous_command(command)) {
    telemetry.completed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    telemetry.exit_code = -1;
    return "Error: Dangerous command blocked";
  }

  try {
    auto future = std::async(std::launch::async, [&]() -> std::string {
      FILE *pipe = Utils::Platform::open_process(
          command + Utils::Platform::get_shell_redirect_both(), "r");

      if (!pipe) {
        telemetry.exit_code = -1;
        return "Error: Failed to open process";
      }

      std::string result;
      char buffer[256];
      size_t total_bytes = 0;
      bool got_first_output = false;

      while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (!got_first_output) {
          telemetry.first_output_at = now;
          got_first_output = true;
        }

        telemetry.last_output_at = now;
        telemetry.lines_streamed++;
        total_bytes += strlen(buffer);

        if (total_bytes > max_bytes) {
          result += "\n[Output truncated]";
          break;
        }
        result += buffer;
      }

      telemetry.exit_code = Utils::Platform::close_process(pipe);
      telemetry.completed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();

      if (telemetry.exit_code != 0) {
        result += "\nExit code: " + std::to_string(telemetry.exit_code);
      }

      return result.empty() ? "Command completed" : result;
    });

    if (future.wait_for(std::chrono::seconds(timeout_seconds)) ==
        std::future_status::timeout) {
      telemetry.timed_out = true;
      telemetry.completed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      return "Error: Command timed out";
    }
    return future.get();
  } catch (const std::exception &e) {
    telemetry.completed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    telemetry.exit_code = -1;
    return std::string("Error executing command: ") + e.what();
  }
}
} // namespace Services
