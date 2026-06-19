#pragma once
#include "agent_mode.h"
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
class CURSOR_API Agent {
public:
  using Mode = AgentMode; // Type alias for backward compatibility

  struct AgentTask {
    int id;
    std::string description;
    bool completed;
  };

private:
  Mode mode_{Mode::MODE_UNSET};
  std::string api_key_;
  std::string ollama_model_;
  bool shell_mode_{false};
  bool verbose_mode_{false};
  std::string active_goal_;
  std::vector<AgentTask> tasks_;
  std::map<std::string, std::string> agent_params_;
  std::unique_ptr<Data::MemoryManager> memory_;
  std::unique_ptr<Services::AIService> ai_service_;
  int command_count_{0};
  long long token_usage_{0};

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
  std::string process_user_input(const std::string &input);
  void handle_direct_command(const std::string &input);
  std::string handle_ai_chat(const std::string &input);
  void handle_file_injection_command(const std::string &input);
  void handle_shell_command(const std::string &input);
  void handle_meta_command(const std::string &input);
  void handle_agentic_command(const std::string &command);
  void show_agentic_help();
  void set_goal(const std::string &goal);
  void show_goal() const;
  void clear_goal();
  void add_task(const std::string &task_description);
  void list_tasks() const;
  void complete_task(const std::string &args);
  void remove_task(const std::string &args);
  void set_param(const std::string &param_string);
  void show_params() const;
  std::string build_agent_context() const;
  static bool is_direct_command_input(const std::string &input);
  static bool is_git_status_query(const std::string &input);
  static std::optional<std::string> map_nl_to_direct_command(
      const std::string &input);

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
  void show_agent_documentation();
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

  void toggle_verbose_mode();
  void show_reasoning_header(const std::string &operation_type);
  void show_pipeline_section(const std::string &section_title);
  void show_reasoning_step(const std::string &label,
                           const std::string &detail);
  void show_parsed_input(const std::string &input,
                         const std::string &parsed_as);
  void show_context_state();
  void show_ai_prompt(const std::string &system_prompt,
                       const std::string &user_input);
  void show_operation_result(const std::string &operation,
                             const std::string &result);
  void show_search_results(const std::string &query,
                           const std::vector<std::string> &results);
  void show_git_status_results(const std::vector<std::string> &files);
  void show_file_preview(const std::string &filename, const std::string &content, int max_lines = 20);

  [[nodiscard]] bool is_online_mode() const;

  Agent();
  ~Agent();

  void run();

  static std::string format_message(const std::string &sender,
                                    const std::string &content);
};
} // namespace Core
