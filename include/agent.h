#pragma once
#include "agent_mode.h"
#include "core/session_state.h"
#include "utils/config.h" // For CURSOR_API
#include <map>
#include <optional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations for our own types

namespace Data {
class MemoryManager;
}
namespace Services {
class AIService;
}

namespace Core {

// Forward declarations for extracted classes
class Startup;
class CommandRouter;
class Session;

class CURSOR_API Agent {
public:
  using Mode = AgentMode; // Type alias for backward compatibility

  struct AgentTask {
    int id;
    std::string description;
    bool completed;
  };

private:
  std::string api_key_;
  bool shell_mode_{false};
  std::string active_goal_;
  std::vector<AgentTask> tasks_;
  std::map<std::string, std::string> agent_params_;
  std::unique_ptr<Data::MemoryManager> memory_;
  std::unique_ptr<Services::AIService> ai_service_;

  friend class Startup;
  friend class CommandRouter;

public:
  SessionState state_;

  // Move operations
  Agent(Agent &&) noexcept;
  Agent &operator=(Agent &&) noexcept;

  // Disable copying
  Agent(const Agent &) = delete;
  Agent &operator=(const Agent &) = delete;

  Agent();
  ~Agent();

  void run();

  bool shell_mode() const noexcept;
  const std::string &active_goal() const noexcept;
  const std::vector<AgentTask> &tasks() const noexcept;
  const std::map<std::string, std::string> &agent_params() const noexcept;

  static std::string format_message(const std::string &sender,
                                     const std::string &content);
};

// Standalone menu function for use outside Agent
int show_menu(const std::string &title,
              const std::vector<std::string> &items,
              int default_index);
} // namespace Core
