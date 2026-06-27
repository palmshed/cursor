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
  void show_memory_context(const std::string &context);
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

  // Discovery display
  struct DiscoveryLines {
    std::string project_type;
    int source_file_count;
    int service_count;
    bool has_tests;
    std::vector<std::string> ci_systems;
    std::vector<std::string> package_managers;
    std::vector<std::string> relevant_files;
    std::vector<std::string> impact_areas;
  };
  void show_discovery_report(const DiscoveryLines &d);

  // Plan display
  struct PlanTaskLine {
    std::string description;
    std::string file_ref;
  };
  void show_task_plan(const std::vector<PlanTaskLine> &tasks);

  // Doctor / verification display
  struct CheckLine {
    std::string name;
    bool passed;
    std::string details;
    std::string fix;
  };
  void show_doctor_report(const std::vector<CheckLine> &checks);

  // Todo / progress display
  void show_todo_list(const std::vector<std::pair<std::string, bool>> &items);

  // Execution trace
  void begin_execution(const std::string &title, int total_steps);
  void step_started(int step, const std::string &label);
  void step_completed(int step, const std::string &label,
                      const std::string &detail = "");
  void step_failed(int step, const std::string &label,
                   const std::string &reason);
  void step_no_evidence(int step, const std::string &label,
                        const std::string &detail = "");
  void end_execution(int succeeded, int failed);

  // Execution summary
  struct ExecutionSummaryData {
    int verified;
    int not_executed;
    int failed;
    std::vector<std::string> files_changed;
    std::string build_result;
    std::string test_result;
  };
  void show_execution_summary(const ExecutionSummaryData &data);

  // Tool visibility — called before each tool invocation
  void show_tool_invocation(const std::string &tool,
                            const std::string &args);

  // Tool output visibility — called after tool completion
  void show_tool_output(const std::string &output);

  // Investigation summary — called after all tools complete
  void show_investigation_complete();

  // Preview proposed changes (before apply)
  void show_preview(const std::vector<PlanTaskLine> &tasks);

  // Apply prompt — returns true if user approves
  static bool prompt_apply();

  // Change Preview display
  struct DiffPreviewFile {
    std::string filename;
    std::string diff_content;  // git diff snippet (+/- lines)
    std::string build_result;
    std::string test_result;
  };
  struct ChangePreviewData {
    std::vector<DiffPreviewFile> files;
    std::string build_result;
    std::string test_result;
    int total_steps;
    int succeeded;
    int failed;
  };
  void show_change_preview(const ChangePreviewData &preview);

  // Progress section for timeline stages
  void show_progress_section(const std::string &section);

  // Tool section name lookup (public for execution engine timeline)
  std::string section_for_tool(const std::string &tool);

  // Spinner
  void spinner(const std::string &message, int duration_ms);
  void spinner(std::atomic<bool> &done);
  void spinner(const std::string &message, std::atomic<bool> &done);

  // Markdown rendering
  static std::string render_markdown(const std::string &text);

private:
  const Agent &agent_;
  std::string last_tool_;
  std::string current_section_;

  bool is_verbose() const;
  static int get_terminal_width();

  static std::string replace_inline_markdown(const std::string &line);
  static std::string highlight_code_line(const std::string &line,
                                         const std::string &language);
  static std::string detect_language_from_filename(
      const std::string &filename);
};

} // namespace Core
