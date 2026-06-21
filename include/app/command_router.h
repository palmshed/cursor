#pragma once
#include "agent.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Services {
class CiInvestigationService;
class DiscoveryService;
class ExecutionEngine;
class PlanningService;
class ReplayService;
class SelfTestService;
class VerificationService;
}

namespace Core {

class UIManager;

class CommandRouter {
public:
  explicit CommandRouter(Agent &agent, UIManager &ui,
                          Services::ReplayService *replay = nullptr);

  std::string process_user_input(const std::string &input);
  void handle_direct_command(const std::string &input);
  std::string handle_ai_chat(const std::string &input);
  void handle_file_injection_command(const std::string &input);
  void handle_shell_command(const std::string &input);
  void handle_meta_command(const std::string &input);
  void handle_agentic_command(const std::string &command);
  void set_goal(const std::string &goal);
  void clear_goal();
  void add_task(const std::string &task_description);
  void complete_task(const std::string &args);
  void remove_task(const std::string &args);
  void set_param(const std::string &param_string);
  std::string build_agent_context() const;
  static bool is_direct_command_input(const std::string &input);
  static bool is_git_status_query(const std::string &input);
  static bool is_codebase_query(const std::string &input);
  static bool is_substantial_task(const std::string &input);
  static std::optional<std::string> map_nl_to_direct_command(
      const std::string &input);
  std::string process_file_injections(const std::string &input);
  std::string read_file_or_directory(const std::string &path);
  void toggle_shell_mode();
  bool should_skip_file(const std::string &file_path, const std::string &ext);
  void toggle_verbose_mode();
  void handle_mode_command(const std::string &arg);
  void handle_chat_management(const std::string &command);
  void add_to_memory(const std::string &text);
  void compress_context();
  void handle_context_management(const std::string &command);
  void handle_multi_file_command(const std::string &command);
  void handle_web_fetch_command(const std::string &command);
  void handle_checkpoint_command(const std::string &command);
  void handle_mcp_command(const std::string &command);
  void handle_theme_command(const std::string &command);
  void handle_auth_command(const std::string &command);
  void handle_sandbox_command(const std::string &command);
  void handle_error_command(const std::string &command);
  void handle_replay_command(const std::string &command);
  void handle_doctor_command();
  void handle_self_test_command();
  void handle_benchmark_command();
  void handle_ci_command(const std::string &command);
  std::string handle_task_with_planning(const std::string &input);
  std::string handle_codebase_query(const std::string &input);
  std::string generate_search_terms(const std::string &input) const;

private:
  Agent &agent_;
  UIManager &ui_;
  Services::ReplayService *replay_;
  std::string discovery_context_;
};

} // namespace Core
