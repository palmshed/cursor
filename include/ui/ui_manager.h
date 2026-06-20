#pragma once
#include "agent.h"
#include <atomic>
#include <string>
#include <vector>

namespace Core {

class UIManager {
public:
  explicit UIManager(const Agent &agent);

  // Pipeline / reasoning display
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
  void show_file_preview(const std::string &filename,
                         const std::string &content, int max_lines = 20);

  // Help / documentation display
  void show_meta_help();
  void show_agentic_help();
  void show_available_tools();
  void show_agent_documentation();
  void show_memory_context();
  void show_session_stats();

  // Goal / task / param display
  void show_goal();
  void list_tasks();
  void show_params();

  // Screen / terminal
  void clear_screen();
  void clear_line();
  void exit_chat_mode();

  // Logo / status / divider
  void print_logo();
  void print_divider();
  void print_ready_interface(const std::string &mode,
                             const std::string &model);

  // Spinner
  void spinner(const std::string &message, int duration_ms);
  void spinner(std::atomic<bool> &done);

  // Markdown rendering
  static std::string render_markdown(const std::string &text);

private:
  const Agent &agent_;

  bool is_verbose() const;
  static int get_terminal_width();

  static std::string replace_inline_markdown(const std::string &line);
  static std::string highlight_code_line(const std::string &line,
                                         const std::string &language);
  static std::string detect_language_from_filename(
      const std::string &filename);
};

} // namespace Core
