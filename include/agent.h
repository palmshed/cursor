#pragma once
#include "agent_mode.h"
#include "utils/config.h" // For CURSOR_API
#include <deque>
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
class CURSOR_API Agent {
public:
  using Mode = AgentMode; // Type alias for backward compatibility

  struct MessageBlock {
    std::string text;
    int lines;
  };

private:
  Mode mode_{Mode::MODE_UNSET};
  std::string api_key_;
  std::string ollama_model_;
  bool shell_mode_{false};
  // Using raw pointers with PIMPL idiom would be better for ABI stability
  std::unique_ptr<Data::MemoryManager> memory_;
  std::unique_ptr<Services::AIService> ai_service_;
  int command_count_{0};
  long long token_usage_{0};

  std::deque<MessageBlock> messages_;
  int scroll_offset_{0};
  int total_lines_{0};

public:
  // Move operations
  Agent(Agent &&) noexcept = default;
  Agent &operator=(Agent &&) noexcept = default;

  // Disable copying
  Agent(const Agent &) = delete;
  Agent &operator=(const Agent &) = delete;

  void initialize_mode();
  int show_menu(const std::string &title,
                const std::vector<std::string> &items,
                int default_index);
  void process_user_input(const std::string &input);
  void handle_direct_command(const std::string &input);
  void handle_ai_chat(const std::string &input);
  void handle_file_injection_command(const std::string &input);
  void handle_shell_command(const std::string &input);
  void handle_meta_command(const std::string &input);

  // File injection helpers
  std::string process_file_injections(const std::string &input);
  std::string read_file_or_directory(const std::string &path);

  // Shell mode management
  void toggle_shell_mode();

  // Helper methods
  bool should_skip_file(const std::string &file_path, const std::string &ext);
  void show_meta_help();
  void clear_screen();
  void handle_chat_management(const std::string &command);
  void show_available_tools();
  void show_memory_context();
  void add_to_memory(const std::string &text);
  void compress_context();
  void show_session_stats();
  void handle_context_management(const std::string &command);
  void handle_multi_file_command(const std::string &command);
  void handle_web_fetch_command(const std::string &command);
  void handle_checkpoint_command(const std::string &command);
  void handle_mcp_command(const std::string &command);
  void handle_theme_command(const std::string &command);
  void handle_auth_command(const std::string &command);
  void handle_sandbox_command(const std::string &command);
  void handle_error_command(const std::string &command);

  [[nodiscard]] bool is_online_mode() const;

  Agent();
  ~Agent();

  void run();

  // Scroll management
  void store_message(const std::string &text);
  void redraw_messages();
  static int count_lines(const std::string &text);
};
} // namespace Core
