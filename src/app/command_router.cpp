#include "app/command_router.h"
#include "app/common.h"
#include "app/menu.h"
#include "app/startup.h"
#include "memory_manager.h"
#include "services/ai_service.h"
#include "services/auth_service.h"
#include "services/checkpoint_service.h"
#include "services/codebase_service.h"
#include "services/command_service.h"
#include "services/context_service.h"
#include "services/error_service.h"
#include "services/file_service.h"
#include "services/git_service.h"
#include "services/github_service.h"
#include "services/mcp_service.h"
#include "services/multi_file_service.h"
#include "services/replay_service.h"
#include "services/sandbox_service.h"
#include "services/theme_service.h"
#include "services/web_service.h"
#include "ui/ui_manager.h"
#include "utils/ui.h"
#include "utils/validation.h"
#include "version.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

// Constants for response handling
const size_t MAX_RESPONSE_LENGTH = 8000;

// ---------------------------------------------------------------------------
// CommandRouter implementation
// ---------------------------------------------------------------------------

namespace Core {

CommandRouter::CommandRouter(Agent &agent, UIManager &ui,
                              Services::ReplayService *replay)
    : agent_(agent), ui_(ui), replay_(replay) {}

std::string CommandRouter::process_user_input(const std::string &input) {
  agent_.state_.command_count_++;
  std::string trimmed_input = trim_copy(input);
  ui_.show_reasoning_header("INPUT RECEIVED");
  ui_.show_pipeline_section("Input classification");
  ui_.show_parsed_input(input, trimmed_input);

  if (agent_.shell_mode_ && !trimmed_input.starts_with("!")) {
    ui_.show_pipeline_section("Shell mode handling");
    ui_.show_reasoning_step("Shell mode", "active");
    ui_.show_reasoning_step("Command", "!" + trimmed_input);
    handle_shell_command("!" + trimmed_input);
    return {};
  }

  // Handle @ file injection commands
  if (trimmed_input.find('@') != std::string::npos) {
    ui_.show_pipeline_section("File injection detection");
    ui_.show_reasoning_step("Detected", "file injection");
    handle_file_injection_command(trimmed_input);
    return {};
  }

  // Handle ! shell commands
  if (trimmed_input.starts_with("!")) {
    ui_.show_pipeline_section("Shell command execution");
    ui_.show_reasoning_step("Command", trimmed_input);
    handle_shell_command(trimmed_input);
    return {};
  }

  // Handle / meta commands
  if (trimmed_input.starts_with("/")) {
    ui_.show_pipeline_section("Meta command execution");
    ui_.show_reasoning_step("Command", trimmed_input);
    handle_meta_command(trimmed_input);
    return {};
  }

  if (is_direct_command_input(trimmed_input)) {
    ui_.show_reasoning_header("DIRECT COMMAND");
    ui_.show_pipeline_section("Direct command execution");
    ui_.show_parsed_input(trimmed_input, "direct command: " + trimmed_input);
    ui_.show_context_state();
    handle_direct_command(trimmed_input);
    return {};
  }
  if (auto mapped_command = map_nl_to_direct_command(trimmed_input)) {
    ui_.show_reasoning_header("NATURAL LANGUAGE TOOLING");
    ui_.show_pipeline_section("Natural language mapping");
    ui_.show_reasoning_step("User intent", trimmed_input);
    ui_.show_reasoning_step("Mapped to", *mapped_command);
    ui_.show_context_state();
    handle_direct_command(*mapped_command);
    return {};
  }

  ui_.show_reasoning_header("AI CHAT");
  ui_.show_pipeline_section("AI reasoning flow");
  ui_.show_context_state();
  return handle_ai_chat(trimmed_input);
}

void CommandRouter::handle_direct_command(const std::string &input) {
  std::string result;

  if (input.starts_with("search:")) {
    std::string query = trim_copy(input.substr(7));
    auto validation = Utils::Validator::validate_search_query(query);
    if (!validation.is_valid) {
      result = "Error: " + validation.error_message;
    } else {
      if (!validation.warnings.empty()) {
        for (const auto &warning : validation.warnings) {
          Utils::UI::print_warning(warning);
        }
      }
      result = Services::WebService::search(query);
      agent_.memory_->save_interaction("search:" + query, result);
    }
  } else if (input.starts_with("cmd:")) {
    std::string command = trim_copy(input.substr(4));
    auto validation = Utils::Validator::validate_command_safe(command);
    if (!validation.warnings.empty()) {
      for (const auto &warning : validation.warnings) {
        Utils::UI::print_warning(warning);
      }
      std::cout << "Continue? (y/N): ";
      std::string confirm;
      std::getline(std::cin, confirm);
      if (confirm != "y" && confirm != "Y") {
        result = "Command cancelled by user";
        agent_.memory_->save_interaction("cmd:" + command, result);
        std::cout << result << std::endl;
        return;
      }
    }
    result = Services::CommandService::execute(command);
    agent_.memory_->save_interaction("cmd:" + command, result);
  } else if (input.starts_with("build:")) {
    handle_direct_command("cmd:" + trim_copy(input.substr(6)));
    return;
  } else if (input.starts_with("read:")) {
    std::string params = trim_copy(input.substr(5));
    // Check if it has range parameters: read:filename:start:count
    size_t first_colon = params.find(':');
    if (first_colon != std::string::npos) {
      std::string filename = params.substr(0, first_colon);
      auto validation = Utils::Validator::validate_file_exists(filename);
      if (!validation.is_valid) {
        result = "Error: " + validation.error_message;
      } else {
        std::string range_params = params.substr(first_colon + 1);
        size_t second_colon = range_params.find(':');

        if (second_colon != std::string::npos) {
          int start_line = std::stoi(range_params.substr(0, second_colon));
          int line_count = std::stoi(range_params.substr(second_colon + 1));
          result = Services::FileService::read_file_range(filename, start_line,
                                                          line_count);
        } else {
          int start_line = std::stoi(range_params);
          result = Services::FileService::read_file_range(filename, start_line);
        }
      }
    } else {
      auto validation = Utils::Validator::validate_file_exists(params);
      if (!validation.is_valid) {
        result = "Error: " + validation.error_message;
      } else {
        result = Services::FileService::read_file(params);
        ui_.show_file_preview(params, result, 30);
      }
    }
    agent_.memory_->save_interaction("read:" + params, result);
  } else if (input.starts_with("write:")) {
    // Format: write:filename content...
    size_t space_pos = input.find(' ', 6); // index 6 is safe for "write:"
    if (space_pos != std::string::npos) {
      std::string filename = trim_copy(input.substr(6, space_pos - 6));
      std::string content = input.substr(space_pos + 1);

      // Validate the file is writable
      auto validation = Utils::Validator::validate_file_writable(filename);
      if (!validation.is_valid) {
        result = "Error: " + validation.error_message;
      } else {
        result = Services::FileService::write_file(filename, content);
        agent_.memory_->save_interaction("write:" + filename, result);
      }
    } else {
      result = "Usage: write:filename content";
    }
  } else if (input.starts_with("replace:")) {
    // Format: replace:filename:old_text:new_text[:expected_count]
    std::string params = input.substr(8);
    std::vector<std::string> parts;
    size_t pos = 0;
    size_t colon_pos;

    // Split by colons (up to 4 parts)
    while ((colon_pos = params.find(':', pos)) != std::string::npos &&
           parts.size() < 3) {
      parts.push_back(params.substr(pos, colon_pos - pos));
      pos = colon_pos + 1;
    }
    parts.push_back(params.substr(pos)); // Last part

    if (parts.size() >= 3) {
      const std::string &filename = parts[0];
      const std::string &old_text = parts[1];
      const std::string &new_text = parts[2];
      int expected = (parts.size() > 3) ? std::stoi(parts[3]) : 1;

      auto edit_result = Services::FileService::replace_text_in_file(
          filename, old_text, new_text, expected);
      result = edit_result.message;
      agent_.memory_->save_interaction("replace:" + filename, result);
    } else {
      result = "Usage: replace:filename:old_text:new_text[:expected_count]";
    }
  } else if (input.starts_with("grep:")) {
    // Format: grep:pattern[:directory[:file_filter]]
    std::string params = input.substr(5);
    std::vector<std::string> parts;
    size_t pos = 0;
    size_t colon_pos;

    while ((colon_pos = params.find(':', pos)) != std::string::npos &&
           parts.size() < 2) {
      parts.push_back(params.substr(pos, colon_pos - pos));
      pos = colon_pos + 1;
    }
    parts.push_back(params.substr(pos));

    std::string pattern = parts[0];
    std::string directory = (parts.size() > 1) ? parts[1] : ".";
    std::string filter = (parts.size() > 2) ? parts[2] : "*";

    auto search_results =
        Services::FileService::search_in_directory(directory, pattern, filter);

    if (search_results.empty()) {
      result = "No matches found for pattern: " + pattern;
    } else {
      result = "Found " + std::to_string(search_results.size()) + " matches:\n";
      std::vector<std::string> formatted_results;
      for (const auto &match : search_results) {
        std::string line_text = match.file_path + ":" + std::to_string(match.line_number) +
                  ": " + match.line_content;
        result += line_text + "\n";
        formatted_results.push_back(line_text);
      }
      ui_.show_search_results(pattern, formatted_results);
    }
    agent_.memory_->save_interaction("grep:" + pattern, result);
  } else if (input.starts_with("remember:")) {
    std::string fact = trim_copy(input.substr(9));
    agent_.memory_->save_global_fact(fact);
    result = "Remembered: " + fact;
  } else if (input.starts_with("forget")) {
    agent_.memory_->clear_global_memory();
    result = "Global memory cleared";
  } else if (input.starts_with("memory")) {
    std::string global_context = agent_.memory_->get_global_context();
    result =
        global_context.empty() ? "No global memories stored" : global_context;
  } else if (input.starts_with("clear")) {
    agent_.memory_->clear_memory();
    result = "Session memory cleared";
  } else if (input.starts_with("analyze:")) {
    std::string path = trim_copy(input.substr(8));
    if (path.empty()) {
      path = "."; // Current directory
    }
    result = Services::CodebaseService::analyze_structure(path);
    agent_.memory_->save_interaction("analyze:" + path, result);
  } else if (input.starts_with("components:")) {
    std::string path = trim_copy(input.substr(11));
    if (path.empty()) {
      path = "."; // Current directory
    }
    result = Services::CodebaseService::find_main_components(path);
    agent_.memory_->save_interaction("components:" + path, result);
  } else if (input.starts_with("todos:")) {
    std::string path = trim_copy(input.substr(6));
    if (path.empty()) {
      path = "."; // Current directory
    }
    auto todos = Services::CodebaseService::find_todos(path);
    if (todos.empty()) {
      result = "No task comments found";
    } else {
      result = "Found " + std::to_string(todos.size()) + " task comments:\n";
      for (const auto &todo : todos) {
        result += todo + "\n";
      }
    }
    agent_.memory_->save_interaction("todos:" + path, result);
  } else if (input.starts_with("git:")) {
    std::string params = trim_copy(input.substr(4));
    std::string path = "."; // Default to current directory

    if (params.find("log") == 0) {
      result = Services::GitService::get_git_log(path, 7);
    } else if (params.find("status") == 0) {
      result = Services::GitService::get_git_status(path);
      auto files = Services::GitService::get_working_tree_changed_files(path);
      ui_.show_git_status_results(files);
    } else if (params.find("analyze") == 0) {
      result = Services::GitService::analyze_repository(path);
    } else {
      result = "Usage: git:log, git:status, git:analyze";
    }
    agent_.memory_->save_interaction("git:" + params, result);
  } else if (input.starts_with("tree:")) {
    std::string path = trim_copy(input.substr(5));
    if (path.empty()) {
      path = "."; // Current directory
    }
    result = Services::CodebaseService::get_directory_tree(path, 3);
    agent_.memory_->save_interaction("tree:" + path, result);
  } else if (input.starts_with("github:")) {
    std::string params = trim_copy(input.substr(7));
    auto parse_repo_spec = [](const std::string &repo_spec, std::string &owner,
                              std::string &repo) -> bool {
      size_t slash_pos = repo_spec.find('/');
      if (slash_pos != std::string::npos) {
        owner = repo_spec.substr(0, slash_pos);
        repo = repo_spec.substr(slash_pos + 1);
        return true;
      }
      return false;
    };
    if (params.starts_with("repo:")) {
      std::string repo_spec = trim_copy(params.substr(5));
      std::string owner, repo;
      if (parse_repo_spec(repo_spec, owner, repo)) {
        result = Services::GitHubService::get_repo_info(owner, repo);
      } else {
        result = "Usage: github:repo:owner/repo";
      }
    } else if (params.starts_with("issues:")) {
      std::string repo_spec = trim_copy(params.substr(7));
      std::string owner, repo;
      if (parse_repo_spec(repo_spec, owner, repo)) {
        auto issues = Services::GitHubService::get_issues(owner, repo);
        if (issues.empty()) {
          result = "No issues found";
        } else {
          result = "Found " + std::to_string(issues.size()) + " issues:\n";
          for (const auto &issue : issues) {
            result +=
                "#" + std::to_string(issue.number) + ": " + issue.title + "\n";
          }
        }
      } else {
        result = "Usage: github:issues:owner/repo";
      }
    } else if (params.starts_with("health:")) {
      std::string repo_spec = trim_copy(params.substr(7));
      std::string owner, repo;
      if (parse_repo_spec(repo_spec, owner, repo)) {
        result = Services::GitHubService::run_health_check(owner, repo);
      } else {
        result = "Usage: github:health:owner/repo";
      }
    } else {
      result = "Usage: github:repo:owner/repo, github:issues:owner/repo, "
               "github:health:owner/repo";
    }
    agent_.memory_->save_interaction("github:" + params, result);
  } else {
    result = "Unknown command";
  }

  ui_.show_operation_result("Operation complete", result);
  if (!result.empty()) {
    std::cout << result << std::endl;
  }
}

std::string CommandRouter::handle_ai_chat(const std::string &input) {
  if (!agent_.ai_service_) {
    agent_.ai_service_ = std::make_unique<Services::AIService>(agent_.state_.mode_, agent_.api_key_);
    if (!agent_.state_.ollama_model_.empty()) {
      agent_.ai_service_->set_model_name(agent_.state_.ollama_model_);
    }
  }

  if (!agent_.ai_service_->is_available()) {
    std::string msg = "AI service unavailable\n";
    std::cout << msg;
    return msg;
  }

  std::atomic<bool> done(false);
  std::thread spin([this, &done]() { ui_.spinner(done); });

  // Build enhanced context with hierarchical context
  std::string memory_context = agent_.memory_->get_context_string();
  std::string hierarchical_context =
      Services::ContextService::load_hierarchical_context(".");

  std::string full_context = memory_context;
  if (!hierarchical_context.empty()) {
    full_context = hierarchical_context + "\n\n" + memory_context;
  }

  std::string agent_context = build_agent_context();
  if (!agent_context.empty()) {
    full_context = agent_context + "\n\n" + full_context;
  }

  std::string system_prompt = "You are an advanced AI agent with comprehensive codebase analysis capabilities.";
  ui_.show_ai_prompt(system_prompt, input);

  std::string response = agent_.ai_service_->chat(input, full_context);

  done = true;
  if (spin.joinable())
    spin.join();

  if (!response.empty()) {
    if (is_tty_stream(stdout)) {
      std::string formatted_response = ui_.render_markdown(response);
      std::cout << agent_.format_message("cursor", formatted_response) << "\n";
    } else {
      std::cout << response << "\n";
    }
    agent_.memory_->save_interaction(input, response);

    return response;
  } else {
    std::cout << "No response\n";
    return "No response\n";
  }
}

void CommandRouter::handle_file_injection_command(const std::string &input) {
  std::string processed_input = process_file_injections(input);
  // Result is printed by the caller via process_user_input's return
  handle_ai_chat(processed_input);
}

void CommandRouter::handle_shell_command(const std::string &input) {
  if (input == "!") {
    // Toggle shell mode
    toggle_shell_mode();
    return;
  }

  // Execute shell command
  std::string command = trim_copy(input.substr(1));
  if (command.empty()) {
    std::cout << "Usage: !<command> or ! to toggle shell mode" << std::endl;
    return;
  }

  auto validation = Utils::Validator::validate_command_safe(command);
  if (!validation.warnings.empty()) {
    for (const auto &warning : validation.warnings) {
      Utils::UI::print_warning(warning);
    }
    std::cout << "Continue? (y/N): ";
    std::string confirm;
    std::getline(std::cin, confirm);
    if (confirm != "y" && confirm != "Y") {
      std::cout << "Command cancelled by user" << std::endl;
      return;
    }
  }

  std::string result = Services::CommandService::execute(command);
  std::cout << result << std::endl;
  agent_.memory_->save_interaction("!" + command, result);
}

void CommandRouter::handle_meta_command(const std::string &input) {
  std::string command = trim_copy(input.substr(1));

  if (command == "help" || command == "?") {
    ui_.show_agent_documentation();
  } else if (command == "docs") {
    ui_.show_agent_documentation();
  } else if (command == "debug") {
    toggle_verbose_mode();
  } else if (command == "clear") {
    ui_.clear_screen();
  } else if (command == "goal" || command.starts_with("goal ") ||
             command == "task" || command.starts_with("task ") ||
             command == "params" || command.starts_with("params ")) {
    handle_agentic_command(command);
  } else if (command.starts_with("chat ")) {
    handle_chat_management(command.substr(5));
  } else if (command == "tools") {
    ui_.show_available_tools();
  } else if (command == "memory show") {
    ui_.show_memory_context();
  } else if (command.starts_with("memory add ")) {
    add_to_memory(command.substr(11));
  } else if (command == "compress") {
    compress_context();
  } else if (command == "stats") {
    ui_.show_session_stats();
  } else if (command.starts_with("context ")) {
    handle_context_management(command.substr(8));
  } else if (command.starts_with("files ")) {
    handle_multi_file_command(command.substr(6));
  } else if (command.starts_with("fetch ")) {
    handle_web_fetch_command(command.substr(6));
  } else if (command.starts_with("checkpoint ")) {
    handle_checkpoint_command(command.substr(11));
  } else if (command == "restore") {
    handle_checkpoint_command("list");
  } else if (command.starts_with("restore ")) {
    handle_checkpoint_command("restore " + command.substr(8));
  } else if (command.starts_with("mcp ")) {
    handle_mcp_command(command.substr(4));
  } else if (command.starts_with("theme ")) {
    handle_theme_command(command.substr(6));
  } else if (command.starts_with("auth ")) {
    handle_auth_command(command.substr(5));
  } else if (command.starts_with("sandbox ")) {
    handle_sandbox_command(command.substr(8));
  } else if (command.starts_with("error ")) {
    handle_error_command(command.substr(6));
  } else if (command.starts_with("replay ")) {
    handle_replay_command(command.substr(7));
  } else if (command == "quit" || command == "exit") {
    std::cout << "Goodbye!" << std::endl;
    exit(0);
  } else {
    std::cout << "Unknown meta command: /" << command << std::endl;
    std::cout << "Type /help for available commands." << std::endl;
  }
}

void CommandRouter::toggle_verbose_mode() {
  agent_.state_.verbose_mode_ = !agent_.state_.verbose_mode_;
  std::cout << (agent_.state_.verbose_mode_ ? Utils::Color::GREEN : Utils::Color::YELLOW)
            << (agent_.state_.verbose_mode_ ? "[debug] Verbose mode ON"
                                : "[debug] Verbose mode OFF")
            << Utils::Color::RESET << std::endl;
}

void CommandRouter::handle_chat_management(const std::string &command) {
  if (command.starts_with("save ")) {
    std::string tag = trim_copy(command.substr(5));
    if (tag.empty()) {
      std::cout << "Usage: /chat save <tag>" << std::endl;
      return;
    }
    agent_.memory_->save_conversation_state(tag);
    std::cout << "Conversation saved as: " << tag << std::endl;
  } else if (command.starts_with("resume ")) {
    std::string tag = trim_copy(command.substr(7));
    if (tag.empty()) {
      std::cout << "Usage: /chat resume <tag>" << std::endl;
      return;
    }
    if (agent_.memory_->resume_conversation_state(tag)) {
      std::cout << "Conversation resumed: " << tag << std::endl;
    } else {
      std::cout << "Could not resume conversation: " << tag << std::endl;
    }
  } else if (command == "list") {
    auto conversations = agent_.memory_->list_conversation_states();
    if (conversations.empty()) {
      std::cout << "No saved conversations." << std::endl;
    } else {
      std::cout << "Saved conversations:" << std::endl;
      for (const auto &conv : conversations) {
        std::cout << "  " << conv << std::endl;
      }
    }
  } else {
    std::cout << "Usage: /chat [save|resume|list] <tag>" << std::endl;
  }
}

void CommandRouter::add_to_memory(const std::string &text) {
  if (text.empty()) {
    std::cout << "Usage: /memory add <text>" << std::endl;
    return;
  }
  agent_.memory_->save_global_fact(text);
  std::cout << "Added to memory: " << text << std::endl;
}

void CommandRouter::compress_context() {
  try {
    std::cout << "Analyzing conversation context for compression..."
              << std::endl;
    std::string compressible_content = agent_.memory_->get_compressible_context();
    if (compressible_content.empty()) {
      std::cout << "Not enough conversation history to compress (need at least "
                   "5 interactions)."
                << std::endl;
      return;
    }
    std::string compression_prompt =
        "Please create a concise summary of the following conversation "
        "history. "
        "Preserve key information, decisions made, important facts discovered, "
        "and context that would be needed for future interactions. "
        "Focus on actionable information and maintain continuity. Keep the "
        "summary under 500 words.\n\n"
        "Conversation History:\n" +
        compressible_content;
    std::cout << "Generating AI-powered summary..." << std::endl;
    std::string summary = agent_.ai_service_->chat(compression_prompt, "");
    if (summary.empty()) {
      std::cout << "Failed to generate compression summary." << std::endl;
      return;
    }
    agent_.memory_->compress_memory(summary);
    std::cout << " Context successfully compressed!" << std::endl;
    std::cout << "Original interactions have been summarized and recent "
                 "context preserved."
              << std::endl;
    std::cout << "Backup created automatically for safety." << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Error during context compression: " << e.what() << std::endl;
  }
}

void CommandRouter::handle_agentic_command(const std::string &command) {
  if (command == "goal" || command == "goal show") {
    ui_.show_goal();
  } else if (command.starts_with("goal set ")) {
    set_goal(trim_copy(command.substr(9)));
  } else if (command == "goal clear") {
    clear_goal();
  } else if (command == "task" || command == "task list") {
    ui_.list_tasks();
  } else if (command.starts_with("task add ")) {
    add_task(trim_copy(command.substr(9)));
  } else if (command.starts_with("task complete ")) {
    complete_task(trim_copy(command.substr(14)));
  } else if (command.starts_with("task remove ")) {
    remove_task(trim_copy(command.substr(12)));
  } else if (command == "params" || command == "params show") {
    ui_.show_params();
  } else if (command.starts_with("params set ")) {
    set_param(trim_copy(command.substr(11)));
  } else if (command == "params clear") {
    agent_.agent_params_.clear();
    std::cout << "Cleared agent parameters." << std::endl;
  } else {
    ui_.show_agentic_help();
  }
}

void CommandRouter::set_goal(const std::string &goal) {
  if (goal.empty()) {
    std::cout << "Usage: /goal set <description>" << std::endl;
    return;
  }
  agent_.active_goal_ = goal;
  agent_.tasks_.clear();
  agent_.agent_params_.clear();
  std::cout << "Goal set: " << agent_.active_goal_ << std::endl;
  agent_.memory_->save_interaction("goal:set", agent_.active_goal_);
}

void CommandRouter::clear_goal() {
  agent_.active_goal_.clear();
  agent_.tasks_.clear();
  agent_.agent_params_.clear();
  std::cout << "Cleared goal, tasks, and parameters." << std::endl;
  agent_.memory_->save_interaction("goal:clear", "Cleared goal and task state");
}

void CommandRouter::add_task(const std::string &task_description) {
  if (task_description.empty()) {
    std::cout << "Usage: /task add <description>" << std::endl;
    return;
  }

  int task_id = agent_.tasks_.empty() ? 1 : agent_.tasks_.back().id + 1;
  agent_.tasks_.push_back({task_id, task_description, false});
  std::cout << "Task added: [" << task_id << "] " << task_description
            << std::endl;
  agent_.memory_->save_interaction("task:add", task_description);
}

void CommandRouter::complete_task(const std::string &args) {
  if (args.empty()) {
    std::cout << "Usage: /task complete <id>" << std::endl;
    return;
  }
  try {
    int task_id = std::stoi(args);
    for (auto &task : agent_.tasks_) {
      if (task.id == task_id) {
        task.completed = true;
        std::cout << "Task completed: [" << task_id << "] "
                  << task.description << std::endl;
        agent_.memory_->save_interaction("task:complete", task.description);
        return;
      }
    }
    std::cout << "Task not found: " << args << std::endl;
  } catch (const std::exception &) {
    std::cout << "Invalid task id: " << args << std::endl;
  }
}

void CommandRouter::remove_task(const std::string &args) {
  if (args.empty()) {
    std::cout << "Usage: /task remove <id>" << std::endl;
    return;
  }
  try {
    int task_id = std::stoi(args);
    auto it = std::remove_if(agent_.tasks_.begin(), agent_.tasks_.end(),
                             [&](const Agent::AgentTask &task) {
                               return task.id == task_id;
                             });
    if (it != agent_.tasks_.end()) {
      std::cout << "Task removed: " << task_id << std::endl;
      agent_.tasks_.erase(it, agent_.tasks_.end());
      agent_.memory_->save_interaction("task:remove", std::to_string(task_id));
    } else {
      std::cout << "Task not found: " << args << std::endl;
    }
  } catch (const std::exception &) {
    std::cout << "Invalid task id: " << args << std::endl;
  }
}

void CommandRouter::set_param(const std::string &param_string) {
  size_t equals_pos = param_string.find('=');
  if (equals_pos == std::string::npos) {
    std::cout << "Usage: /params set key=value" << std::endl;
    return;
  }
  std::string key = trim_copy(param_string.substr(0, equals_pos));
  std::string value = trim_copy(param_string.substr(equals_pos + 1));
  if (key.empty() || value.empty()) {
    std::cout << "Usage: /params set key=value" << std::endl;
    return;
  }
  agent_.agent_params_[key] = value;
  std::cout << "Parameter set: " << key << " = " << value << std::endl;
  agent_.memory_->save_interaction("params:set", key + "=" + value);
}

std::string CommandRouter::build_agent_context() const {
  if (agent_.active_goal_.empty() && agent_.tasks_.empty() && agent_.agent_params_.empty()) {
    return "";
  }

  std::ostringstream context;
  if (!agent_.active_goal_.empty()) {
    context << "Current objective: " << agent_.active_goal_ << "\n";
  }
  if (!agent_.tasks_.empty()) {
    context << "Tasks:\n";
    for (const auto &task : agent_.tasks_) {
      context << "- [" << (task.completed ? "x" : " ") << "] "
              << task.id << ": " << task.description << "\n";
    }
  }
  if (!agent_.agent_params_.empty()) {
    context << "Parameters:\n";
    for (const auto &pair : agent_.agent_params_) {
      context << "- " << pair.first << " = " << pair.second << "\n";
    }
  }
  context << "Use this goal, task list, and parameters to guide your next actions.";
  return context.str();
}

bool CommandRouter::is_direct_command_input(const std::string &input) {
  static const std::array<std::string_view, 3> direct_aliases = {
      "memory", "clear", "forget"};
  for (const auto &alias : direct_aliases) {
    if (input == alias) {
      return true;
    }
  }

  static const std::array<std::string_view, 14> direct_prefixes = {
      "search:", "cmd:",    "build:", "read:",  "write:",
      "replace:", "grep:",  "remember:", "analyze:", "components:",
      "todos:",   "git:",   "tree:",  "github:"};
  for (const auto &prefix : direct_prefixes) {
    if (input.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

bool CommandRouter::is_git_status_query(const std::string &input) {
  std::string lower = normalize_input(input);

  static const std::vector<std::string> triggers = {
      "files we changed",
      "changed files",
      "show changed files",
      "what changed",
      "check changed files",
      "check the files we changed",
      "what files changed",
      "what changes",
      "what has changed",
      "git diff",
      "git status"};

  for (const auto &phrase : triggers) {
    if (lower.find(phrase) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> CommandRouter::map_nl_to_direct_command(
    const std::string &input) {
  std::string lower = normalize_input(input);
  // Helper to extract the remainder after a marker
  auto extract_after = [&](const std::vector<std::string> &markers)
      -> std::optional<std::string> {
    for (const auto &m : markers) {
      size_t pos = lower.find(m);
      if (pos != std::string::npos) {
        std::string val = trim_copy(input.substr(pos + m.size()));
        if (!val.empty())
          return std::make_optional(val);
      }
    }
    return std::nullopt;
  };

  // Git-related queries
  if (is_git_status_query(input) || lower.find("what changed") != std::string::npos) {
    return std::make_optional(std::string("git:status"));
  }
  if (lower.find("git diff") != std::string::npos ||
      lower.find("show diff") != std::string::npos ||
      lower.find("diff") != std::string::npos) {
    return std::make_optional(std::string("git:status"));
  }
  if (lower.find("git log") != std::string::npos ||
      lower.find("commit history") != std::string::npos ||
      lower.find("recent commits") != std::string::npos) {
    return std::make_optional(std::string("git:log"));
  }

  // Run/execute shell commands
  if (auto cmd = extract_after({"run ", "execute ", "start "})) {
    return std::make_optional(std::string("cmd:") + *cmd);
  }

  // Build / compile requests
  if (lower.find("build the project") != std::string::npos ||
      lower.find("build project") != std::string::npos ||
      lower.find("compile") != std::string::npos ||
      lower.find("build") == 0) {
    // Prefer explicit make invocation when mentioned, otherwise use build wrapper
    if (auto make_cmd = extract_after({"make "})) {
      return std::make_optional(std::string("cmd:") + *make_cmd);
    }
    return std::make_optional(std::string("build:make"));
  }

  // TODO / task comment queries (prefer over grep)
  if (lower.find("todo") != std::string::npos || lower.find("task comments") != std::string::npos || lower.find("fixme") != std::string::npos) {
    return std::make_optional(std::string("todos:."));
  }

  // Grep/search-style queries
  if (lower.find("search for ") != std::string::npos &&
      (lower.find("code") != std::string::npos ||
       lower.find("repo") != std::string::npos ||
       lower.find("project") != std::string::npos)) {
    return extract_grep_command(input, "search for ");
  }
  if ((lower.find("find ") != std::string::npos &&
       (lower.find("in code") != std::string::npos ||
        lower.find("in repo") != std::string::npos ||
        lower.find("in project") != std::string::npos ||
        lower.find("in files") != std::string::npos)) ||
      lower.find("grep ") != std::string::npos) {
    if (auto g = extract_after({"find ", "grep ", "search for "}))
      return extract_grep_command(*g, "");
  }

  // Read/open file
  if (auto path = extract_after({"read file ", "show file ", "open file "})) {
    return std::make_optional(std::string("read:") + *path);
  }

  // Write/save file
  if (lower.find("save file ") != std::string::npos ||
      lower.find("write file ") != std::string::npos ||
      lower.find("save ") == 0) {
    if (auto p = extract_after({"save file ", "write file ", "save "})) {
      // If user provided content like "save file X as Y", attempt to parse
      size_t as_pos = normalize_input(*p).find(" as ");
      if (as_pos != std::string::npos) {
        std::string filename = trim_copy(p->substr(0, as_pos));
        std::string content = trim_copy(p->substr(as_pos + 4));
        if (!filename.empty() && !content.empty())
          return std::make_optional(std::string("write:") + filename + " " + content);
      }
      // Fallback: just write filename (caller will validate usage)
      return std::make_optional(std::string("write:") + *p);
    }
  }

  // Replace text in files: "replace X with Y in file Z"
  if (lower.find("replace ") != std::string::npos && lower.find(" with ") != std::string::npos) {
    // crude parsing
    size_t rep_pos = lower.find("replace ");
    size_t with_pos = lower.find(" with ");
    std::string old_text = trim_copy(input.substr(rep_pos + 8, with_pos - (rep_pos + 8)));
    size_t in_pos = lower.find(" in file ");
    if (in_pos == std::string::npos)
      in_pos = lower.find(" in ");
    if (in_pos != std::string::npos) {
      std::string new_text = trim_copy(input.substr(with_pos + 6, in_pos - (with_pos + 6)));
      std::string filename = trim_copy(input.substr(in_pos + (lower.find(" in file ") != std::string::npos ? 9 : 4)));
      if (!filename.empty() && !old_text.empty() && !new_text.empty()) {
        return std::make_optional(std::string("replace:") + filename + ":" + old_text + ":" + new_text);
      }
    }
  }

  // Remember / forget / memory / clear
  if (auto fact = extract_after({"remember ", "remember:"})) {
    return std::make_optional(std::string("remember:") + *fact);
  }
  if (lower.find("forget") != std::string::npos) {
    return std::make_optional(std::string("forget"));
  }
  if (lower.find("show memory") != std::string::npos || lower.find("memory show") != std::string::npos || lower == "memory") {
    return std::make_optional(std::string("memory"));
  }
  if (lower.find("clear memory") != std::string::npos || lower == "clear") {
    return std::make_optional(std::string("clear"));
  }

  // Analyze / components / todos / tree
  if (lower.find("analyze repo") != std::string::npos || lower.find("analyze") == 0 || lower.find("repository analysis") != std::string::npos) {
    return std::make_optional(std::string("analyze:."));
  }
  if (lower.find("components") != std::string::npos) {
    return std::make_optional(std::string("components:."));
  }
  if (lower.find("todo") != std::string::npos || lower.find("task comments") != std::string::npos) {
    return std::make_optional(std::string("todos:."));
  }
  if (lower.find("directory tree") != std::string::npos || lower.find("repo tree") != std::string::npos || lower.find("tree ") != std::string::npos) {
    return std::make_optional(std::string("tree:."));
  }

  // GitHub quick helpers: "open repo owner/repo on github" or "github repo owner/repo"
  if (lower.find("on github") != std::string::npos) {
    // try to find owner/repo earlier in the sentence
    size_t slash = lower.find('/');
    if (slash != std::string::npos) {
      // extract token around slash
      size_t start = lower.rfind(' ', slash);
      if (start == std::string::npos) start = 0; else start++;
      size_t end = lower.find(' ', slash);
      if (end == std::string::npos) end = lower.size();
      std::string repo_spec = trim_copy(input.substr(start, end - start));
      return std::make_optional(std::string("github:repo:") + repo_spec);
    }
  }

  // Fallback: no mapping
  return std::nullopt;
}

std::string CommandRouter::process_file_injections(const std::string &input) {
  std::string result = input;
  size_t pos = 0;

  // Find all @ symbols and process file paths
  while ((pos = result.find('@', pos)) != std::string::npos) {
    // Skip if it's escaped or part of an email
    if (pos > 0 && result[pos - 1] == '\\') {
      pos++;
      continue;
    }

    // Find the end of the path (space, newline, or end of string)
    size_t start = pos + 1;
    size_t end = start;

    // Handle quoted paths
    bool quoted = false;
    if (start < result.length() && result[start] == '"') {
      quoted = true;
      start++;
      end = result.find('"', start);
      if (end == std::string::npos)
        end = result.length();
    } else {
      // Find end of unquoted path
      while (end < result.length() && result[end] != ' ' &&
             result[end] != '\n' && result[end] != '\t' &&
             result[end] != '\r') {
        end++;
      }
    }

    if (start < end) {
      std::string path = result.substr(start, end - start);
      std::string file_content = read_file_or_directory(path);

      // Replace @path with file content
      size_t replace_start = pos;
      size_t replace_end = quoted ? end + 1 : end;
      result.replace(replace_start, replace_end - replace_start, file_content);

      pos = replace_start + file_content.length();
    } else {
      pos++;
    }
  }

  return result;
}

std::string CommandRouter::read_file_or_directory(const std::string &path) {
  try {
    if (!std::filesystem::exists(path)) {
      return "[File not found: " + path + "]";
    }

    if (std::filesystem::is_directory(path)) {
      // Use multi-file service for directory reading
      Services::MultiFileOptions options;
      options.max_files = 50; // Reasonable limit for @ injection
      options.max_total_size = 5 * 1024 * 1024; // 5MB limit

      auto files =
          Services::MultiFileService::read_directory_files(path, options);

      if (files.empty()) {
        return "[No readable files found in directory: " + path + "]";
      }

      return Services::MultiFileService::format_multi_file_content(
          files, "Contents of directory: " + path);
    } else {
      // Read single file
      std::ifstream file(path);
      if (!file.is_open()) {
        return "[Could not read file: " + path + "]";
      }

      std::ostringstream content;
      content << "[Contents of file: " << path << "]\n\n";

      std::string line;
      int line_count = 0;
      while (std::getline(file, line) && line_count < 1000) {
        content << line << "\n";
        line_count++;
      }

      if (line_count >= 1000) {
        content << "\n[File truncated - too long]";
      }

      return content.str();
    }
  } catch (const std::exception &e) {
    return "[Error reading " + path + ": " + e.what() + "]";
  }
}

void CommandRouter::toggle_shell_mode() {
  agent_.shell_mode_ = !agent_.shell_mode_;
  if (agent_.shell_mode_) {
    std::cout << "Entering shell mode. Type commands directly or '!' to exit."
              << std::endl;
  } else {
    std::cout << "Exiting shell mode." << std::endl;
  }
}

bool CommandRouter::should_skip_file(const std::string &file_path,
                             const std::string &ext) {
  std::vector<std::string> skip_extensions = {
      ".exe", ".dll",  ".so",  ".dylib", ".a",   ".lib", ".obj", ".o",   ".png",
      ".jpg", ".jpeg", ".gif", ".bmp",   ".ico", ".svg", ".mp3", ".mp4", ".avi",
      ".mov", ".wav",  ".pdf", ".zip",   ".tar", ".gz",  ".7z",  ".rar"};

  for (const auto &skip_ext : skip_extensions) {
    if (ext == skip_ext)
      return true;
  }

  if (file_path.find("node_modules") != std::string::npos ||
      file_path.find(".git") != std::string::npos ||
      file_path.find("build") != std::string::npos ||
      file_path.find("dist") != std::string::npos ||
      file_path.find(".cache") != std::string::npos) {
    return true;
  }

  return false;
}


void CommandRouter::handle_context_management(const std::string &command) {
  if (command == "show") {
    std::string context =
        Services::ContextService::load_hierarchical_context(".");
    if (context.empty()) {
      std::cout << "No hierarchical context found." << std::endl;
      std::cout << "Create a CURSOR.md file with /context create"
                << std::endl;
    } else {
      std::cout << "Hierarchical Context:" << std::endl;
      std::cout << context << std::endl;
    }
  } else if (command == "refresh") {
    Services::ContextService::refresh_context_cache();
    std::cout << "Context cache refreshed." << std::endl;
  } else if (command == "create") {
    if (Services::ContextService::create_context_file(".")) {
      std::cout << "Created CURSOR.md in current directory." << std::endl;
      std::cout << "Edit it to provide context for this project." << std::endl;
    } else {
      std::cout << "Could not create CURSOR.md (file may already exist)."
                << std::endl;
    }
  } else {
    std::cout << "Usage: /context [show|refresh|create]" << std::endl;
  }
}

void CommandRouter::handle_multi_file_command(const std::string &command) {
  if (command.empty()) {
    std::cout << "Usage: /files <path1> [path2] [--include pattern] [--exclude "
                 "pattern]"
              << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  /files src/" << std::endl;
    std::cout << "  /files . --include *.cpp --include *.h" << std::endl;
    std::cout << "  /files src/ --exclude *.log --exclude build/*" << std::endl;
    return;
  }

  // Parse command arguments
  std::vector<std::string> paths;
  Services::MultiFileOptions options;

  std::istringstream iss(command);
  std::string token;
  std::string current_flag;

  while (iss >> token) {
    if (token == "--include") {
      current_flag = "include";
    } else if (token == "--exclude") {
      current_flag = "exclude";
    } else if (token == "--no-gitignore") {
      options.respect_gitignore = false;
      current_flag.clear();
    } else if (token == "--no-recursive") {
      options.recursive = false;
      current_flag.clear();
    } else {
      if (current_flag == "include") {
        options.include_patterns.push_back(token);
      } else if (current_flag == "exclude") {
        options.exclude_patterns.push_back(token);
      } else {
        paths.push_back(token);
      }
    }
  }

  if (paths.empty()) {
    paths.push_back("."); // Default to current directory
  }

  // Read files
  auto files = Services::MultiFileService::read_many_files(paths, options);

  if (files.empty()) {
    std::cout << "No files found matching the criteria." << std::endl;
    return;
  }

  // Format and display
  std::string formatted = Services::MultiFileService::format_multi_file_content(
      files, "Multi-file read result");

  std::cout << formatted << std::endl;

  // Save to memory for AI context
  agent_.memory_->save_interaction("/files " + command, formatted);
}

void CommandRouter::handle_web_fetch_command(const std::string &command) {
  if (command.empty()) {
    std::cout << "Usage: /fetch <url> [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  text    - Extract text content from HTML" << std::endl;
    std::cout << "  json    - Parse and format JSON response" << std::endl;
    std::cout << "  raw     - Return raw response content" << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  /fetch https://api.github.com/users/octocat json"
              << std::endl;
    std::cout << "  /fetch https://example.com text" << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string url, format = "text";
  iss >> url >> format;

  if (url.empty()) {
    std::cout << "Error: URL is required" << std::endl;
    return;
  }

  if (!Services::WebService::is_valid_url(url)) {
    std::cout << "Error: Invalid URL format. Use http:// or https://"
              << std::endl;
    return;
  }

  std::cout << "Fetching: " << url << std::endl;

  std::string result;
  if (format == "json") {
    result = Services::WebService::fetch_json(url);
  } else if (format == "raw") {
    auto response = Services::WebService::fetch_url(url);
    if (response.success) {
      result = "Status: " + std::to_string(response.status_code) + "\n";
      result += "Content-Type: " + response.content_type + "\n\n";
      result += response.content;
      if (result.length() > MAX_RESPONSE_LENGTH) {
        result = result.substr(0, MAX_RESPONSE_LENGTH) +
                 "\n\n[Content truncated - showing first " +
                 std::to_string(MAX_RESPONSE_LENGTH) + " characters]";
      }
    } else {
      result = "Error: " + response.error_message;
    }
  } else { // default to text
    result = Services::WebService::fetch_text(url);
  }

  // Process the fetched content with AI if needed
  if (result.length() > 1000) { // Only process large content with AI
    std::atomic<bool> done(false);
    std::thread spin([this, &done]() { ui_.spinner(done); });

    std::string context = agent_.memory_->get_context_string();
    std::string ai_prompt = "Summarize the following content:\n\n" + result;
    std::string response = agent_.ai_service_->chat(ai_prompt, context);

    done = true;
    if (spin.joinable())
      spin.join();

    if (!response.empty()) {
      std::cout << "\nAI Summary:\n" << response << std::endl;
      agent_.memory_->save_interaction("web_fetch_summary", response);
    }
  }

  std::cout << result << std::endl;

  // Save to memory for AI context
  agent_.memory_->save_interaction("/fetch " + command, result);
}

void CommandRouter::handle_checkpoint_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Checkpoint commands:" << std::endl;
    std::cout
        << "  /checkpoint create <name> [description] - Create new checkpoint"
        << std::endl;
    std::cout
        << "  /checkpoint list                       - List all checkpoints"
        << std::endl;
    std::cout
        << "  /checkpoint info <id>                  - Show checkpoint details"
        << std::endl;
    std::cout << "  /checkpoint delete <id>                - Delete checkpoint"
              << std::endl;
    std::cout << "  /checkpoint cleanup [count]            - Keep only N "
                 "recent checkpoints"
              << std::endl;
    std::cout << "  /restore                               - List checkpoints "
                 "for restore"
              << std::endl;
    std::cout
        << "  /restore <id>                          - Restore from checkpoint"
        << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    if (action == "create") {
      std::string name, description;
      iss >> name;
      std::getline(iss, description);
      if (!description.empty() && description[0] == ' ') {
        description = description.substr(1); // Remove leading space
      }

      if (name.empty()) {
        std::cout << "Error: Checkpoint name is required" << std::endl;
        std::cout << "Usage: /checkpoint create <name> [description]"
                  << std::endl;
        return;
      }

      std::cout << "Creating checkpoint '" << name << "'..." << std::endl;
      std::string checkpoint_id =
          Services::CheckpointService::create_checkpoint(name, description);
      std::cout << " Checkpoint created with ID: " << checkpoint_id
                << std::endl;

    } else if (action == "list") {
      auto checkpoints = Services::CheckpointService::list_checkpoints();
      if (checkpoints.empty()) {
        std::cout << "No checkpoints found." << std::endl;
        return;
      }

      std::cout << "Available checkpoints:" << std::endl;
      for (const auto &cp : checkpoints) {
        std::cout << "  " << cp.id << " - " << cp.name << std::endl;
        std::cout << "    Created: " << cp.timestamp << std::endl;
        if (!cp.description.empty()) {
          std::cout << "    Description: " << cp.description << std::endl;
        }
        std::cout << "    Files: " << cp.backed_up_files.size() << " ("
                  << (cp.total_size / 1024) << " KB)" << std::endl;
        std::cout << std::endl;
      }

    } else if (action == "info") {
      std::string checkpoint_id;
      iss >> checkpoint_id;
      if (checkpoint_id.empty()) {
        std::cout << "Error: Checkpoint ID is required" << std::endl;
        return;
      }

      auto info =
          Services::CheckpointService::get_checkpoint_info(checkpoint_id);
      std::cout << "Checkpoint Details:" << std::endl;
      std::cout << "  ID: " << info.id << std::endl;
      std::cout << "  Name: " << info.name << std::endl;
      std::cout << "  Created: " << info.timestamp << std::endl;
      if (!info.description.empty()) {
        std::cout << "  Description: " << info.description << std::endl;
      }
      std::cout << "  Total Size: " << (info.total_size / 1024) << " KB"
                << std::endl;
      std::cout << "  Files (" << info.backed_up_files.size()
                << "):" << std::endl;
      for (const auto &file : info.backed_up_files) {
        std::cout << "    " << file << std::endl;
      }

    } else if (action == "delete") {
      std::string checkpoint_id;
      iss >> checkpoint_id;
      if (checkpoint_id.empty()) {
        std::cout << "Error: Checkpoint ID is required" << std::endl;
        return;
      }

      if (Services::CheckpointService::delete_checkpoint(checkpoint_id)) {
        std::cout << " Checkpoint " << checkpoint_id << " deleted" << std::endl;
      } else {
        std::cout << "Error: Failed to delete checkpoint " << checkpoint_id
                  << std::endl;
      }

    } else if (action == "cleanup") {
      int keep_count = 10;
      iss >> keep_count;
      if (keep_count < 1)
        keep_count = 10;

      Services::CheckpointService::cleanup_old_checkpoints(keep_count);
      std::cout << " Cleaned up old checkpoints, keeping " << keep_count
                << " most recent" << std::endl;

    } else if (action == "restore") {
      std::string checkpoint_id;
      iss >> checkpoint_id;

      if (checkpoint_id.empty()) {
        // List checkpoints for selection
        auto checkpoints = Services::CheckpointService::list_checkpoints();
        if (checkpoints.empty()) {
          std::cout << "No checkpoints available for restore." << std::endl;
          return;
        }

        std::cout << "Available checkpoints for restore:" << std::endl;
        for (const auto &cp : checkpoints) {
          std::cout << "  " << cp.id << " - " << cp.name << " (" << cp.timestamp
                    << ")" << std::endl;
        }
        std::cout << std::endl << "Use: /restore <checkpoint_id>" << std::endl;
        return;
      }

      std::cout << "Restoring from checkpoint " << checkpoint_id << "..."
                << std::endl;
      std::cout
          << "Note: A backup will be created automatically before restore."
          << std::endl;

      Services::RestoreOptions options;
      options.create_backup_before_restore = true;
      options.restore_memory = true;
      options.restore_files = true;

      if (Services::CheckpointService::restore_checkpoint(checkpoint_id,
                                                          options)) {
        std::cout << " Successfully restored from checkpoint " << checkpoint_id
                  << std::endl;
        std::cout << "Note: You may need to restart the application to see all "
                     "changes."
                  << std::endl;
      } else {
        std::cout << "Error: Failed to restore from checkpoint "
                  << checkpoint_id << std::endl;
      }

    } else {
      std::cout << "Unknown checkpoint command: " << action << std::endl;
      std::cout << "Use '/checkpoint help' for available commands."
                << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  // Save to memory for AI context
  agent_.memory_->save_interaction("/checkpoint " + command,
                            "Checkpoint command executed");
}

void CommandRouter::handle_mcp_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "MCP (Model Context Protocol) commands:" << std::endl;
    std::cout << "  /mcp servers                           - List registered "
                 "MCP servers"
              << std::endl;
    std::cout << "  /mcp status <server>                   - Show server status"
              << std::endl;
    std::cout << "  /mcp start <server>                    - Start MCP server"
              << std::endl;
    std::cout << "  /mcp stop <server>                     - Stop MCP server"
              << std::endl;
    std::cout
        << "  /mcp resources <server>                - List server resources"
        << std::endl;
    std::cout << "  /mcp read <server> <uri>               - Read resource "
                 "from server"
              << std::endl;
    std::cout << "  /mcp tools <server>                    - List server tools"
              << std::endl;
    std::cout << "  /mcp call <server> <tool> [args]       - Call server tool"
              << std::endl;
    std::cout
        << "  /mcp prompts <server>                  - List server prompts"
        << std::endl;
    std::cout << "  /mcp prompt <server> <name> [args]     - Get server prompt"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    // Initialize MCP service configuration
    Services::MCPService::load_server_config();

    if (action == "servers") {
      auto servers = Services::MCPService::list_mcp_servers();
      if (servers.empty()) {
        std::cout << "No MCP servers registered." << std::endl;
        std::cout
            << "Default servers (filesystem, git) will be added automatically."
            << std::endl;
        return;
      }

      std::cout << "Registered MCP servers:" << std::endl;
      for (const auto &server_name : servers) {
        auto server = Services::MCPService::get_mcp_server(server_name);
        std::string status =
            Services::MCPService::get_server_status(server_name);
        std::cout << "  " << server_name << " - " << status << std::endl;
        std::cout << "    Executable: " << server.executable;
        for (const auto &arg : server.args) {
          std::cout << " " << arg;
        }
        std::cout << std::endl;
      }

    } else if (action == "status") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      std::string status = Services::MCPService::get_server_status(server_name);
      std::cout << "Server '" << server_name << "' status: " << status
                << std::endl;

    } else if (action == "start") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      std::cout << "Starting MCP server '" << server_name << "'..."
                << std::endl;
      if (Services::MCPService::is_server_running(server_name)) {
        std::cout << "Server is already running." << std::endl;
      } else {
        // This would start the actual server in a full implementation
        std::cout << " MCP server '" << server_name << "' started (simulated)"
                  << std::endl;
        std::cout << "Note: Full MCP server integration requires process "
                     "management implementation."
                  << std::endl;
      }

    } else if (action == "stop") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      std::cout << "Stopping MCP server '" << server_name << "'..."
                << std::endl;
      std::cout << " MCP server '" << server_name << "' stopped (simulated)"
                << std::endl;

    } else if (action == "resources") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      auto resources = Services::MCPService::list_resources(server_name);
      if (resources.empty()) {
        std::cout << "No resources available from server '" << server_name
                  << "'" << std::endl;
        return;
      }

      std::cout << "Resources from server '" << server_name
                << "':" << std::endl;
      for (const auto &resource : resources) {
        std::cout << "  " << resource.uri << " - " << resource.name
                  << std::endl;
        if (!resource.description.empty()) {
          std::cout << "    Description: " << resource.description << std::endl;
        }
        if (!resource.mime_type.empty()) {
          std::cout << "    Type: " << resource.mime_type << std::endl;
        }
      }

    } else if (action == "read") {
      std::string server_name, uri;
      iss >> server_name >> uri;
      if (server_name.empty() || uri.empty()) {
        std::cout << "Error: Server name and URI are required" << std::endl;
        return;
      }

      std::cout << "Reading resource '" << uri << "' from server '"
                << server_name << "'..." << std::endl;
      std::string content =
          Services::MCPService::read_resource(server_name, uri);
      std::cout << content << std::endl;

    } else if (action == "tools") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      auto tools = Services::MCPService::list_tools(server_name);
      if (tools.empty()) {
        std::cout << "No tools available from server '" << server_name << "'"
                  << std::endl;
        return;
      }

      std::cout << "Tools from server '" << server_name << "':" << std::endl;
      for (const auto &tool : tools) {
        std::cout << "  " << tool.name << " - " << tool.description
                  << std::endl;
      }

    } else if (action == "call") {
      std::string server_name, tool_name, args_str;
      iss >> server_name >> tool_name;
      std::getline(iss, args_str);
      if (!args_str.empty() && args_str[0] == ' ') {
        args_str = args_str.substr(1); // Remove leading space
      }

      if (server_name.empty() || tool_name.empty()) {
        std::cout << "Error: Server name and tool name are required"
                  << std::endl;
        return;
      }

      nlohmann::json args = nlohmann::json::object();
      if (!args_str.empty()) {
        try {
          args = nlohmann::json::parse(args_str);
        } catch (const std::exception &) {
          // If not valid JSON, treat as simple string argument
          args["input"] = args_str;
        }
      }

      std::cout << "Calling tool '" << tool_name << "' on server '"
                << server_name << "'..." << std::endl;
      auto result =
          Services::MCPService::call_tool(server_name, tool_name, args);
      std::cout << result.dump(2) << std::endl;

    } else if (action == "prompts") {
      std::string server_name;
      iss >> server_name;
      if (server_name.empty()) {
        std::cout << "Error: Server name is required" << std::endl;
        return;
      }

      auto prompts = Services::MCPService::list_prompts(server_name);
      if (prompts.empty()) {
        std::cout << "No prompts available from server '" << server_name << "'"
                  << std::endl;
        return;
      }

      std::cout << "Prompts from server '" << server_name << "':" << std::endl;
      for (const auto &prompt : prompts) {
        std::cout << "  " << prompt.name << " - " << prompt.description
                  << std::endl;
        if (!prompt.arguments.empty()) {
          std::cout << "    Arguments: ";
          for (size_t i = 0; i < prompt.arguments.size(); ++i) {
            if (i > 0)
              std::cout << ", ";
            std::cout << prompt.arguments[i];
          }
          std::cout << std::endl;
        }
      }

    } else if (action == "prompt") {
      std::string server_name, prompt_name, args_str;
      iss >> server_name >> prompt_name;
      std::getline(iss, args_str);
      if (!args_str.empty() && args_str[0] == ' ') {
        args_str = args_str.substr(1); // Remove leading space
      }

      if (server_name.empty() || prompt_name.empty()) {
        std::cout << "Error: Server name and prompt name are required"
                  << std::endl;
        return;
      }

      nlohmann::json args = nlohmann::json::object();
      if (!args_str.empty()) {
        try {
          args = nlohmann::json::parse(args_str);
        } catch (const std::exception &) {
          // If not valid JSON, treat as simple string argument
          args["input"] = args_str;
        }
      }

      std::cout << "Getting prompt '" << prompt_name << "' from server '"
                << server_name << "'..." << std::endl;
      std::string prompt_text =
          Services::MCPService::get_prompt(server_name, prompt_name, args);
      std::cout << prompt_text << std::endl;

    } else {
      std::cout << "Unknown MCP command: " << action << std::endl;
      std::cout << "Use '/mcp help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  // Save to memory for AI context
  agent_.memory_->save_interaction("/mcp " + command, "MCP command executed");
}

void CommandRouter::handle_theme_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Theme commands:" << std::endl;
    std::cout
        << "  /theme list                            - List available themes"
        << std::endl;
    std::cout << "  /theme set <name>                      - Set active theme"
              << std::endl;
    std::cout
        << "  /theme preview <name>                  - Preview theme colors"
        << std::endl;
    std::cout << "  /theme current                         - Show current theme"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::ThemeService::initialize();

    if (action == "list") {
      auto themes = Services::ThemeService::list_available_themes();
      std::cout << "Available themes:" << std::endl;
      for (const auto &theme_name : themes) {
        auto theme_info = Services::ThemeService::get_theme_info(theme_name);
        std::string current =
            (theme_name == Services::ThemeService::get_current_theme())
                ? " (current)"
                : "";
        std::cout << "  " << Services::ThemeService::colorize_accent(theme_name)
                  << current << std::endl;
        std::cout << "    " << theme_info.description << std::endl;
      }

    } else if (action == "set") {
      std::string theme_name;
      iss >> theme_name;
      if (theme_name.empty()) {
        std::cout << "Error: Theme name is required" << std::endl;
        return;
      }

      if (Services::ThemeService::set_theme(theme_name)) {
        std::cout << Services::ThemeService::colorize_success(
                         " Theme set to: " + theme_name)
                  << std::endl;
      } else {
        std::cout << Services::ThemeService::colorize_error(
                         "Error: Theme not found: " + theme_name)
                  << std::endl;
      }

    } else if (action == "preview") {
      std::string theme_name;
      iss >> theme_name;
      if (theme_name.empty()) {
        std::cout << "Error: Theme name is required" << std::endl;
        return;
      }

      Services::ThemeService::print_theme_preview(theme_name);

    } else if (action == "current") {
      std::string current_theme = Services::ThemeService::get_current_theme();
      auto theme_info = Services::ThemeService::get_theme_info(current_theme);
      std::cout << "Current theme: "
                << Services::ThemeService::colorize_accent(current_theme)
                << std::endl;
      std::cout << "Description: " << theme_info.description << std::endl;

    } else {
      std::cout << "Unknown theme command: " << action << std::endl;
      std::cout << "Use '/theme help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  agent_.memory_->save_interaction("/theme " + command, "Theme command executed");
}

void CommandRouter::handle_auth_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Authentication commands:" << std::endl;
    std::cout
        << "  /auth providers                        - List available providers"
        << std::endl;
    std::cout
        << "  /auth set <provider>                   - Set active provider"
        << std::endl;
    std::cout
        << "  /auth key <provider> <key>             - Set API key for provider"
        << std::endl;
    std::cout
        << "  /auth status [provider]                - Show provider status"
        << std::endl;
    std::cout << "  /auth test <provider>                   - Test provider "
                 "connection"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::AuthService::initialize();

    if (action == "providers") {
      auto providers = Services::AuthService::list_providers();
      std::cout << "Available authentication providers:" << std::endl;
      for (const auto &provider_name : providers) {
        auto provider_info =
            Services::AuthService::get_provider_info(provider_name);
        std::string status =
            Services::AuthService::get_provider_status(provider_name);
        std::string active = provider_info.is_active ? " (active)" : "";
        std::cout << "  " << provider_info.display_name << active << " - "
                  << status << std::endl;
        std::cout << "    Model: " << provider_info.model << std::endl;
      }

    } else if (action == "set") {
      std::string provider_name;
      iss >> provider_name;
      if (provider_name.empty()) {
        std::cout << "Error: Provider name is required" << std::endl;
        return;
      }

      if (Services::AuthService::set_active_provider(provider_name)) {
        std::cout << " Active provider set to: " << provider_name << std::endl;
      } else {
        std::cout << "Error: Provider not found: " << provider_name
                  << std::endl;
      }

    } else if (action == "key") {
      std::string provider_name, api_key;
      iss >> provider_name >> api_key;
      if (provider_name.empty() || api_key.empty()) {
        std::cout << "Error: Provider name and API key are required"
                  << std::endl;
        return;
      }

      if (Services::AuthService::set_api_key(provider_name, api_key)) {
        std::cout << " API key set for provider: " << provider_name
                  << std::endl;
      } else {
        std::cout << "Error: Failed to set API key for provider: "
                  << provider_name << std::endl;
      }

    } else if (action == "status") {
      std::string provider_name;
      iss >> provider_name;
      if (provider_name.empty()) {
        provider_name = Services::AuthService::get_active_provider();
      }

      std::string status =
          Services::AuthService::get_provider_status(provider_name);
      auto provider_info =
          Services::AuthService::get_provider_info(provider_name);
      std::cout << "Provider: " << provider_info.display_name << std::endl;
      std::cout << "Status: " << status << std::endl;
      std::cout << "Model: " << provider_info.model << std::endl;
      std::cout << "Base URL: " << provider_info.base_url << std::endl;

    } else if (action == "test") {
      std::string provider_name;
      iss >> provider_name;
      if (provider_name.empty()) {
        std::cout << "Error: Provider name is required" << std::endl;
        return;
      }

      if (Services::AuthService::test_provider_connection(provider_name)) {
        std::cout << " Connection test successful for: " << provider_name
                  << std::endl;
      } else {
        std::cout << " Connection test failed for: " << provider_name
                  << std::endl;
      }

    } else {
      std::cout << "Unknown auth command: " << action << std::endl;
      std::cout << "Use '/auth help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  agent_.memory_->save_interaction("/auth " + command, "Auth command executed");
}

void CommandRouter::handle_sandbox_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Sandbox commands:" << std::endl;
    std::cout << "  /sandbox run <command>                 - Execute command "
                 "in sandbox"
              << std::endl;
    std::cout << "  /sandbox list                          - List available "
                 "sandbox configs"
              << std::endl;
    std::cout << "  /sandbox status                        - Show Docker status"
              << std::endl;
    std::cout
        << "  /sandbox containers                    - List active containers"
        << std::endl;
    std::cout
        << "  /sandbox cleanup                       - Clean up old containers"
        << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::SandboxService::initialize();

    if (action == "run") {
      std::string cmd;
      std::getline(iss, cmd);
      if (!cmd.empty() && cmd[0] == ' ') {
        cmd = cmd.substr(1);
      }
      if (cmd.empty()) {
        std::cout << "Error: Command is required" << std::endl;
        return;
      }

      std::cout << "Executing in sandbox: " << cmd << std::endl;
      auto result = Services::SandboxService::execute_command(cmd);

      std::cout << "Exit code: " << result.exit_code << std::endl;
      std::cout << "Execution time: " << result.execution_time_seconds << "s"
                << std::endl;
      if (!result.stdout_output.empty()) {
        std::cout << "Output:\n" << result.stdout_output << std::endl;
      }
      if (!result.error_message.empty()) {
        std::cout << "Error: " << result.error_message << std::endl;
      }

    } else if (action == "list") {
      auto configs = Services::SandboxService::list_sandbox_configs();
      std::cout << "Available sandbox configurations:" << std::endl;
      for (const auto &config_name : configs) {
        auto config = Services::SandboxService::get_sandbox_config(config_name);
        std::cout << "  " << config_name << " - " << config.image << std::endl;
        std::cout << "    Memory: " << config.memory_limit_mb
                  << "MB, CPU: " << config.cpu_limit_percent << "%"
                  << std::endl;
      }

    } else if (action == "status") {
      if (Services::SandboxService::check_docker_installation()) {
        std::cout << "Docker Status: Available" << std::endl;
        std::cout << "Version: "
                  << Services::SandboxService::get_docker_version()
                  << std::endl;
      } else {
        std::cout << "Docker Status: Not Available" << std::endl;
        std::cout << "Please install Docker to use sandbox features."
                  << std::endl;
      }

    } else if (action == "containers") {
      auto containers = Services::SandboxService::list_active_containers();
      if (containers.empty()) {
        std::cout << "No active sandbox containers." << std::endl;
      } else {
        std::cout << "Active sandbox containers:" << std::endl;
        for (const auto &container : containers) {
          std::cout << "  " << container << std::endl;
        }
      }

    } else if (action == "cleanup") {
      Services::SandboxService::cleanup_old_containers();
      std::cout << " Cleaned up old sandbox containers" << std::endl;

    } else {
      std::cout << "Unknown sandbox command: " << action << std::endl;
      std::cout << "Use '/sandbox help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  agent_.memory_->save_interaction("/sandbox " + command, "Sandbox command executed");
}

void CommandRouter::handle_error_command(const std::string &command) {
  if (command.empty() || command == "help") {
    std::cout << "Error management commands:" << std::endl;
    std::cout << "  /error report                          - Show error summary"
              << std::endl;
    std::cout << "  /error recent [count]                  - Show recent errors"
              << std::endl;
    std::cout << "  /error clear                           - Clear error log"
              << std::endl;
    std::cout << "  /error export <path>                   - Export error log"
              << std::endl;
    return;
  }

  std::istringstream iss(command);
  std::string action;
  iss >> action;

  try {
    Services::ErrorService::initialize();

    if (action == "report") {
      Services::ErrorService::print_error_report();

    } else if (action == "recent") {
      int count = 5;
      iss >> count;
      if (count < 1)
        count = 5;

      auto recent_errors = Services::ErrorService::get_recent_errors(count);
      if (recent_errors.empty()) {
        std::cout << "No recent errors." << std::endl;
      } else {
        std::cout << "Recent errors (" << recent_errors.size()
                  << "):" << std::endl;
        for (const auto &error : recent_errors) {
          std::cout << Services::ErrorService::format_error(error, true)
                    << std::endl
                    << std::endl;
        }
      }

    } else if (action == "clear") {
      Services::ErrorService::clear_error_log();
      std::cout << " Error log cleared" << std::endl;

    } else if (action == "export") {
      std::string export_path;
      iss >> export_path;
      if (export_path.empty()) {
        export_path = "error_log_export.json";
      }

      if (Services::ErrorService::export_error_log(export_path)) {
        std::cout << " Error log exported to: " << export_path << std::endl;
      } else {
        std::cout << "Error: Failed to export error log" << std::endl;
      }

    } else {
      std::cout << "Unknown error command: " << action << std::endl;
      std::cout << "Use '/error help' for available commands." << std::endl;
    }

  } catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
  }

  agent_.memory_->save_interaction("/error " + command, "Error command executed");
}

// ---------------------------------------------------------------------------
// Replay command handler
// ---------------------------------------------------------------------------

static void print_state_diff(const Core::SessionState &before,
                              const Core::SessionState &after) {
  std::vector<std::string> changes;
  if (before.mode_ != after.mode_)
    changes.push_back("mode " + std::to_string(static_cast<int>(before.mode_)) +
                      "\xe2\x86\x92" + std::to_string(static_cast<int>(after.mode_)));
  if (before.ollama_model_ != after.ollama_model_)
    changes.push_back("model " + before.ollama_model_ + "\xe2\x86\x92" +
                      after.ollama_model_);
  if (before.verbose_mode_ != after.verbose_mode_)
    changes.push_back("verbose " + std::string(before.verbose_mode_ ? "on" : "off") +
                      "\xe2\x86\x92" + (after.verbose_mode_ ? "on" : "off"));
  if (before.command_count_ != after.command_count_)
    changes.push_back("cmd#" + std::to_string(before.command_count_) +
                      "\xe2\x86\x92" + std::to_string(after.command_count_));
  if (before.token_usage_ != after.token_usage_)
    changes.push_back("tokens " + std::to_string(before.token_usage_) +
                      "\xe2\x86\x92" + std::to_string(after.token_usage_));

  if (changes.empty()) {
    std::cout << "      state: (no change)\n";
  } else {
    for (auto &c : changes)
      std::cout << "      " << c << "\n";
  }
}

void CommandRouter::handle_replay_command(const std::string &command) {
  if (!replay_) {
    std::cout << "Replay service not available.\n";
    return;
  }

  if (command == "list") {
    auto sessions = replay_->list_sessions();
    if (sessions.empty()) {
      std::cout << "No replay sessions found.\n";
      return;
    }
    std::cout << "Replay sessions:\n";
    for (auto &s : sessions) {
      std::cout << "  " << s.id << "  " << s.created << "  "
                << s.event_count << " events  \"" << s.first_input << "\"\n";
    }

  } else if (command.starts_with("show ")) {
    std::string id = trim_copy(command.substr(5));
    if (id.empty()) {
      std::cout << "Usage: /replay show <session-id>\n";
      return;
    }
    auto events = replay_->load_session(id);
    if (events.empty()) {
      std::cout << "Session not found: " << id << "\n";
      return;
    }
    std::cout << "Session " << id << " (" << events.size() << " events):\n\n";
    for (auto &ev : events) {
      std::cout << "  [" << ev.step << "] t=" << ev.timestamp << "\n";
      std::cout << "      input: " << ev.input << "\n";
      print_state_diff(ev.state_before, ev.state_after);
    }

  } else if (command.starts_with("step ")) {
    std::string id = trim_copy(command.substr(5));
    if (id.empty()) {
      std::cout << "Usage: /replay step <session-id>\n";
      return;
    }
    auto events = replay_->load_session(id);
    if (events.empty()) {
      std::cout << "Session not found: " << id << "\n";
      return;
    }
    std::cout << "Stepping through session " << id << ":\n";
    for (size_t i = 0; i < events.size(); i++) {
      auto &ev = events[i];
      std::cout << "\n--- [" << ev.step << "] t=" << ev.timestamp << " ---\n";
      std::cout << "Input: " << ev.input << "\n";
      print_state_diff(ev.state_before, ev.state_after);
      if (i + 1 < events.size()) {
        std::cout << "Press Enter to continue, q to quit... " << std::flush;
        int ch = std::cin.get();
        if (ch == 'q' || ch == 'Q')
          break;
      }
    }
    std::cout << "\nEnd of session.\n";

  } else if (command.starts_with("play ")) {
    std::string id = trim_copy(command.substr(5));
    if (id.empty()) {
      std::cout << "Usage: /replay play <session-id>\n";
      return;
    }
    auto events = replay_->load_session(id);
    if (events.empty()) {
      std::cout << "Session not found: " << id << "\n";
      return;
    }
    std::cout << "Replaying session " << id << " (" << events.size()
              << " steps)...\n\n";
    for (auto &ev : events) {
      std::cout << "[" << ev.step << "] " << ev.input << "\n";
      process_user_input(ev.input);
      std::cout << "\n";
    }
    std::cout << "Replay complete.\n";

  } else {
    std::cout << "Usage:\n";
    std::cout << "  /replay list              List saved sessions\n";
    std::cout << "  /replay show <id>         Show full session transcript\n";
    std::cout << "  /replay step <id>         Step through session\n";
    std::cout << "  /replay play <id>         Re-execute all commands\n";
  }
}

} // namespace Core
