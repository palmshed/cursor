#include "ui/ui_manager.h"
#include "services/file_service.h"
#include "utils/ui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace Core {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

UIManager::UIManager(const Agent &agent) : agent_(agent) {}

bool UIManager::is_verbose() const {
  return agent_.state_.verbose_mode_;
}

// ---------------------------------------------------------------------------
// Markdown rendering (static helpers)
// ---------------------------------------------------------------------------

std::string UIManager::replace_inline_markdown(const std::string &line) {
  std::string out;
  bool bold = false;
  bool code = false;
  for (size_t i = 0; i < line.size(); i++) {
    if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
      out += bold ? Utils::Color::RESET : Utils::Color::BOLD;
      bold = !bold;
      i++;
    } else if (line[i] == '`') {
      out += code ? Utils::Color::RESET : Utils::Color::YELLOW;
      code = !code;
    } else {
      out += line[i];
    }
  }
  if (bold || code) {
    out += Utils::Color::RESET;
  }
  return out;
}

std::string UIManager::highlight_code_line(const std::string &line,
                                           const std::string &language) {
  std::string out;
  std::string token;
  auto flush_token = [&]() {
    if (token.empty()) return;
    static const std::vector<std::string> keywords = {
        "auto", "bool", "break", "case", "class", "const", "continue",
        "else", "false", "for", "function", "if", "int", "let", "return",
        "std", "string", "struct", "true", "var", "void", "while"};
    bool is_keyword =
        std::find(keywords.begin(), keywords.end(), token) != keywords.end();
    out += is_keyword ? Utils::Color::CYAN + token + Utils::Color::RESET : token;
    token.clear();
  };
  for (size_t i = 0; i < line.size(); i++) {
    char ch = line[i];
    if (language == "html" && (ch == '<' || ch == '>')) {
      flush_token();
      out += Utils::Color::CYAN + std::string(1, ch) + Utils::Color::RESET;
    } else if (ch == '"' || ch == '\'') {
      flush_token();
      char quote = ch;
      out += Utils::Color::YELLOW + std::string(1, ch);
      i++;
      while (i < line.size()) {
        out += line[i];
        if (line[i] == quote) break;
        i++;
      }
      out += Utils::Color::RESET;
    } else if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
      token += ch;
    } else {
      flush_token();
      out += ch;
    }
  }
  flush_token();
  return out;
}

std::string UIManager::render_markdown(const std::string &text) {
  std::stringstream input(text);
  std::ostringstream output;
  std::string line;
  bool in_code = false;
  std::string language;

  while (std::getline(input, line)) {
    // simple trim for detection only
    size_t a = 0;
    while (a < line.size() && std::isspace(static_cast<unsigned char>(line[a])))
      a++;
    size_t b = line.size();
    while (b > a && std::isspace(static_cast<unsigned char>(line[b - 1])))
      b--;
    std::string trimmed = line.substr(a, b - a);

    if (trimmed.starts_with("```")) {
      in_code = !in_code;
      language = in_code ? trimmed.substr(3) : "";
      if (!language.empty()) {
        a = 0;
        while (a < language.size() && std::isspace(static_cast<unsigned char>(language[a])))
          a++;
        b = language.size();
        while (b > a && std::isspace(static_cast<unsigned char>(language[b - 1])))
          b--;
        language = language.substr(a, b - a);
      }
      if (in_code && !language.empty()) {
        output << Utils::Color::DIM << language << Utils::Color::RESET << "\n";
      }
      continue;
    }

    if (in_code) {
      output << "  " << highlight_code_line(line, language) << "\n";
    } else if (trimmed.starts_with("#")) {
      size_t pos = trimmed.find_first_not_of("# ");
      output << Utils::Color::BOLD
             << (pos == std::string::npos ? trimmed : trimmed.substr(pos))
             << Utils::Color::RESET << "\n";
    } else {
      output << replace_inline_markdown(line) << "\n";
    }
  }
  return output.str();
}

std::string UIManager::detect_language_from_filename(
    const std::string &filename) {
  auto pos = filename.find_last_of('.');
  if (pos == std::string::npos) return "";
  std::string ext = filename.substr(pos + 1);
  if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c")
    return "cpp";
  if (ext == "h" || ext == "hpp" || ext == "hh")
    return "cpp";
  if (ext == "py") return "python";
  if (ext == "js" || ext == "ts") return "javascript";
  if (ext == "md") return "markdown";
  if (ext == "html" || ext == "htm") return "html";
  return "";
}

// ---------------------------------------------------------------------------
// Pipeline / reasoning display
// ---------------------------------------------------------------------------

void UIManager::show_reasoning_header(const std::string &operation_type) {
  if (!is_verbose()) return;
  std::cout << "\n" << Utils::Color::CYAN << "=== " << Utils::Color::BOLD
            << operation_type << Utils::Color::RESET << Utils::Color::CYAN
            << " ===" << Utils::Color::RESET << std::endl;
}

void UIManager::show_pipeline_section(const std::string &section_title) {
  if (!is_verbose()) return;
  std::cout << "\n" << Utils::Color::BOLD << Utils::Color::CYAN << "[ "
            << section_title << " ]" << Utils::Color::RESET << std::endl;
}

void UIManager::show_reasoning_step(const std::string &label,
                                    const std::string &detail) {
  if (!is_verbose()) return;
  std::cout << Utils::Color::CYAN << "  - " << Utils::Color::RESET << label;
  if (!detail.empty()) {
    std::cout << ": " << Utils::Color::YELLOW << detail << Utils::Color::RESET;
  }
  std::cout << std::endl;
}

void UIManager::show_parsed_input(const std::string &input,
                                  const std::string &parsed_as) {
  if (!is_verbose()) return;
  show_reasoning_step("Heard", input);
  show_reasoning_step("Parsed as", parsed_as);
}

void UIManager::show_context_state() {
  if (!is_verbose()) return;
  std::cout << Utils::Color::PINK << "Agent Context:" << Utils::Color::RESET
            << std::endl;
  if (!agent_.active_goal().empty()) {
    std::cout << "  Goal: " << agent_.active_goal() << std::endl;
  }
  if (!agent_.tasks().empty()) {
    std::cout << "  Tasks: " << agent_.tasks().size() << " active";
    int completed = 0;
    for (const auto &t : agent_.tasks()) {
      if (t.completed) completed++;
    }
    if (completed > 0)
      std::cout << " (" << completed << " completed)";
    std::cout << std::endl;
  }
  if (!agent_.agent_params().empty()) {
    std::cout << "  Params: " << agent_.agent_params().size() << " set" << std::endl;
  }
}

void UIManager::show_ai_prompt(const std::string &system_prompt,
                               const std::string &user_input) {
  if (!is_verbose()) return;
  show_pipeline_section("AI prompt construction");
  show_reasoning_step("System prompt length",
                      std::to_string(system_prompt.length()) + " chars");
  show_reasoning_step("User input preview",
                      user_input.length() > 100
                          ? user_input.substr(0, 100) + "..."
                          : user_input);
}

void UIManager::show_operation_result(const std::string &operation,
                                      const std::string &result) {
  if (!is_verbose()) return;
  std::cout << Utils::Color::GREEN << "[ok] " << operation << ": "
            << Utils::Color::RESET;
  if (result.length() > 150) {
    std::cout << result.substr(0, 150) << "..." << std::endl;
  } else {
    std::cout << result << std::endl;
  }
}

void UIManager::show_search_results(
    const std::string &query,
    const std::vector<std::string> &results) {
  if (!is_verbose() || results.empty()) return;
  std::cout << "\n" << Utils::Color::GREEN << "Search results for '"
            << query << "' (" << results.size() << " matches)"
            << Utils::Color::RESET << std::endl;
  int line_num = 1;
  for (const auto &result : results) {
    std::cout << Utils::Color::CYAN << std::setw(4) << std::right << line_num
              << Utils::Color::RESET << ": " << result << std::endl;
    line_num++;
  }
}

void UIManager::show_git_status_results(
    const std::vector<std::string> &files) {
  if (!is_verbose()) return;
  if (files.empty()) {
    std::cout << Utils::Color::GREEN << "[ok] Working directory clean"
              << Utils::Color::RESET << std::endl;
    return;
  }
  std::cout << "\n" << Utils::Color::YELLOW << "Modified / Untracked files ("
            << files.size() << ")" << Utils::Color::RESET << std::endl;
  int i = 1;
  for (const auto &f : files) {
    std::cout << Utils::Color::PINK << std::setw(3) << i << Utils::Color::RESET
              << " " << f << std::endl;
    i++;
  }
}

void UIManager::show_file_preview(const std::string &filename,
                                  const std::string &content, int max_lines) {
  if (!is_verbose()) return;
  if (content.rfind("Error:", 0) == 0) {
    std::cout << Utils::Color::RED << content << Utils::Color::RESET
              << std::endl;
    return;
  }
  std::string language = detect_language_from_filename(filename);
  std::istringstream in(content);
  std::string line;
  int line_no = 1;
  std::cout << "\n" << Utils::Color::GREEN << filename << Utils::Color::RESET
            << std::endl;
  while (line_no <= max_lines && std::getline(in, line)) {
    std::ostringstream pref;
    pref << Utils::Color::CYAN << std::setw(4) << std::right << line_no
         << Utils::Color::RESET << ": ";
    if (!language.empty()) {
      std::cout << pref.str() << highlight_code_line(line, language)
                << std::endl;
    } else {
      std::cout << pref.str() << replace_inline_markdown(line) << std::endl;
    }
    line_no++;
  }
  if (in && !in.eof()) {
    std::cout << Utils::Color::DIM << "... (file truncated)"
              << Utils::Color::RESET << std::endl;
  }
}

// ---------------------------------------------------------------------------
// Help / documentation display
// ---------------------------------------------------------------------------

void UIManager::show_meta_help() {
  std::cout << "Available meta commands:" << std::endl;
  std::cout << "  /help or /?             - Show this help" << std::endl;
  std::cout << "  /debug                  - Toggle verbose/debug mode" << std::endl;
  std::cout << "  /clear                  - Clear screen" << std::endl;
  std::cout << "  /goal set <description> - Set a project goal" << std::endl;
  std::cout << "  /goal show              - Show current goal and task status" << std::endl;
  std::cout << "  /goal clear             - Clear goal, tasks, and params" << std::endl;
  std::cout << "  /task add <description> - Add a task for the current goal" << std::endl;
  std::cout << "  /task list              - List active tasks" << std::endl;
  std::cout << "  /task complete <id>     - Mark a task complete" << std::endl;
  std::cout << "  /task remove <id>       - Remove a task" << std::endl;
  std::cout << "  /params set key=value   - Set goal/task parameters" << std::endl;
  std::cout << "  /params show            - Show current parameters" << std::endl;
  std::cout << "  /params clear           - Clear current parameters" << std::endl;
  std::cout << "  /chat save <tag>        - Save conversation state" << std::endl;
  std::cout << "  /chat resume <tag>      - Resume conversation state" << std::endl;
  std::cout << "  /chat list              - List saved conversations" << std::endl;
  std::cout << "  /tools                  - Show available tools" << std::endl;
  std::cout << "  /memory show            - Show memory context" << std::endl;
  std::cout << "  /memory add <text>      - Add to memory" << std::endl;
  std::cout << "  /compress               - Compress conversation context" << std::endl;
  std::cout << "  /stats                  - Show session statistics" << std::endl;
  std::cout << "  /context show           - Show hierarchical context" << std::endl;
  std::cout << "  /context refresh        - Refresh context cache" << std::endl;
  std::cout << "  /context create         - Create CURSOR.md file" << std::endl;
  std::cout << "  /files <patterns>       - Read multiple files with patterns" << std::endl;
  std::cout << "  /fetch <url> [format]   - Fetch web content (text/json/raw)" << std::endl;
  std::cout << "  /checkpoint <cmd>       - Manage checkpoints (create/list/delete)" << std::endl;
  std::cout << "  /restore [id]           - List or restore from checkpoint" << std::endl;
  std::cout << "  /mcp <cmd>              - MCP server management (servers/tools/resources)" << std::endl;
  std::cout << "  /theme <cmd>            - Theme management (list/set/preview)" << std::endl;
  std::cout << "  /auth <cmd>             - Authentication management (providers/keys)" << std::endl;
  std::cout << "  /sandbox <cmd>          - Sandboxed command execution" << std::endl;
  std::cout << "  /error <cmd>            - Error management and reporting" << std::endl;
  std::cout << "  /github repo:owner/repo - Get repository info" << std::endl;
  std::cout << "  /github issues:owner/repo - List repository issues" << std::endl;
  std::cout << "  /github health:owner/repo - Run health check" << std::endl;
  std::cout << std::endl;
  std::cout << "File injection commands:" << std::endl;
  std::cout << "  @<path>                - Include file/directory content" << std::endl;
  std::cout << "  Example: @src/main.cpp What does this code do?" << std::endl;
  std::cout << std::endl;
  std::cout << "Shell commands:" << std::endl;
  std::cout << "  !<command>             - Execute shell command" << std::endl;
  std::cout << "  !                      - Toggle shell mode" << std::endl;
  std::cout << "  /docs                  - Show agent documentation" << std::endl;
}

void UIManager::show_agentic_help() {
  std::cout << "Agentic workflow commands:" << std::endl;
  std::cout << "  /goal set <description> - Set or update the project goal" << std::endl;
  std::cout << "  /goal show              - Show active goal and tasks" << std::endl;
  std::cout << "  /goal clear             - Clear the current goal and tasks" << std::endl;
  std::cout << "  /task add <description> - Add a task to the current goal" << std::endl;
  std::cout << "  /task list              - List active tasks" << std::endl;
  std::cout << "  /task complete <id>     - Mark a task as completed" << std::endl;
  std::cout << "  /task remove <id>       - Remove a task" << std::endl;
  std::cout << "  /params set key=value   - Set structured goal/task parameters" << std::endl;
  std::cout << "  /params show            - Show task parameters" << std::endl;
  std::cout << "  /params clear           - Clear current task parameters" << std::endl;
}

void UIManager::show_available_tools() {
  std::cout << "Available tools:" << std::endl;
  std::cout << "  File Operations:" << std::endl;
  std::cout << "    read:file[:start:count]     - Read file content" << std::endl;
  std::cout << "    write:file content          - Write to file" << std::endl;
  std::cout << "    replace:file:old:new        - Replace text in file" << std::endl;
  std::cout << "    grep:pattern[:dir[:ext]]    - Search in files" << std::endl;
  std::cout << "    build:command               - Execute build or shell commands" << std::endl;
  std::cout << "  Project Analysis:" << std::endl;
  std::cout << "    analyze:[path]              - Analyze project structure" << std::endl;
  std::cout << "    components:[path]           - Find main components" << std::endl;
  std::cout << "    todos:[path]                - Find task comments" << std::endl;
  std::cout << "    tree:[path]                 - Show directory tree" << std::endl;
  std::cout << "  Git Operations:" << std::endl;
  std::cout << "    git:status                  - Show git status" << std::endl;
  std::cout << "    git:log                     - Show git log" << std::endl;
  std::cout << "    git:analyze                 - Analyze repository" << std::endl;
  std::cout << "  System:" << std::endl;
  std::cout << "    cmd:command                 - Execute shell command" << std::endl;
  std::cout << "    search:query                - Web search" << std::endl;
  std::cout << "    remember:fact               - Save to memory" << std::endl;
  std::cout << "    memory                      - Show memories" << std::endl;
  std::cout << "  GitHub Operations:" << std::endl;
  std::cout << "    github:repo:owner/repo      - Get repository info" << std::endl;
  std::cout << "    github:issues:owner/repo    - List repository issues" << std::endl;
  std::cout << "    github:health:owner/repo    - Run health check" << std::endl;
}

void UIManager::show_agent_documentation() {
  std::string help_path = "AGENTS.md";
  std::string doc = Services::FileService::read_file(help_path);
  if (doc.starts_with("Error:")) {
    std::filesystem::path current = std::filesystem::current_path();
    while (true) {
      std::filesystem::path candidate = current / "AGENTS.md";
      if (Services::FileService::file_exists(candidate.string())) {
        help_path = candidate.string();
        doc = Services::FileService::read_file(help_path);
        break;
      }
      if (!current.has_parent_path() || current == current.parent_path())
        break;
      current = current.parent_path();
    }
  }

  if (doc.starts_with("Error:")) {
    std::cout << "Unable to load runtime help from AGENTS.md: " << doc
              << std::endl;
    std::cout << "Falling back to built-in help.\n";
    show_meta_help();
    return;
  }

  std::cout << "Runtime help from AGENTS.md:\n" << doc << std::endl;
}

void UIManager::show_memory_context(const std::string &context) {
  if (context.empty()) {
    std::cout << "No memory context available." << std::endl;
  } else {
    std::cout << "Current memory context:" << std::endl;
    std::cout << context << std::endl;
  }
}

void UIManager::show_session_stats() {
  std::string mode_str =
      (agent_.state_.mode_ == Agent::Mode::MODE_TOGETHER   ? "Together AI"
       : agent_.state_.mode_ == Agent::Mode::MODE_CEREBRAS ? "Cerebras"
       : agent_.state_.mode_ == Agent::Mode::MODE_FIREWORKS ? "Fireworks"
       : agent_.state_.mode_ == Agent::Mode::MODE_GROQ ? "Groq"
       : agent_.state_.mode_ == Agent::Mode::MODE_DEEPSEEK ? "DeepSeek"
       : agent_.state_.mode_ == Agent::Mode::MODE_OPENAI ? "OpenAI"
       : agent_.state_.mode_ == Agent::Mode::MODE_LLAMA_3B ? "Llama 3B"
       : agent_.state_.mode_ == Agent::Mode::MODE_LLAMA_LATEST ? "Llama Latest"
       : agent_.state_.mode_ == Agent::Mode::MODE_LLAMA_31 ? "Llama 3.1"
                                                     : "Local Ollama");
  std::cout << "Session Statistics:" << std::endl;
  std::cout << "  Mode: " << mode_str << std::endl;
  std::cout << "  Shell Mode: " << (agent_.shell_mode() ? "Active" : "Inactive")
            << std::endl;
  std::cout << "  Commands Processed: " << agent_.state_.command_count_ << std::endl;
  std::cout << "  Token Usage: " << agent_.state_.token_usage_ << std::endl;
}

// ---------------------------------------------------------------------------
// Goal / task / param display
// ---------------------------------------------------------------------------

void UIManager::show_goal() {
  if (agent_.active_goal().empty()) {
    std::cout << "No active goal set." << std::endl;
    return;
  }
  std::cout << "Active goal: " << agent_.active_goal() << std::endl;
  if (agent_.tasks().empty()) {
    std::cout << "No tasks added yet." << std::endl;
  } else {
    std::cout << "Tasks:" << std::endl;
    for (const auto &task : agent_.tasks()) {
      std::cout << "  [" << (task.completed ? "x" : " ") << "] "
                << task.id << ": " << task.description << std::endl;
    }
  }
  if (!agent_.agent_params().empty()) {
    std::cout << "Parameters:" << std::endl;
    for (const auto &pair : agent_.agent_params()) {
      std::cout << "  " << pair.first << " = " << pair.second << std::endl;
    }
  }
}

void UIManager::list_tasks() {
  if (agent_.tasks().empty()) {
    std::cout << "No active tasks." << std::endl;
    return;
  }
  std::cout << "Active tasks:" << std::endl;
  for (const auto &task : agent_.tasks()) {
    std::cout << "  [" << (task.completed ? "x" : " ") << "] "
              << task.id << ": " << task.description << std::endl;
  }
}

void UIManager::show_params() {
  if (agent_.agent_params().empty()) {
    std::cout << "No parameters set." << std::endl;
    return;
  }
  std::cout << "Agent parameters:" << std::endl;
  for (const auto &pair : agent_.agent_params()) {
    std::cout << "  " << pair.first << " = " << pair.second << std::endl;
  }
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

void UIManager::clear_screen() {
  std::cout << "\033[2J\033[H" << std::flush;
}

void UIManager::clear_line() {
  std::cout << "\033[2K\r";
}

void UIManager::exit_chat_mode() {
  std::cout << "\033[?25h";
}

void UIManager::print_logo() {
  std::cout << Utils::Color::BOLD << "▌ CURSOR\n" << Utils::Color::RESET;
}

int UIManager::get_terminal_width() {
#ifndef _WIN32
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
    return w.ws_col;
#endif
  return 80;
}

void UIManager::print_divider() {
  std::string div;
  int w = get_terminal_width();
  div.reserve(w * 3);
  for (int i = 0; i < w; i++)
    div += "─";
  std::cout << Utils::Color::DIM << div << "\n"
            << Utils::Color::RESET;
}

void UIManager::print_ready_interface(const std::string &mode,
                                       const std::string &model,
                                       const std::string &perm_mode) {
  std::cout << Utils::Color::DIM << "[" << mode << " | " << model;
  if (!perm_mode.empty())
    std::cout << " | " << perm_mode;
  std::cout << "]" << Utils::Color::RESET << "\n";
}

void UIManager::spinner(const std::string &message, int duration_ms) {
  const std::array<std::string_view, 10> frames = {
      "⠋","⠙","⠹","⠸","⠼",
      "⠴","⠦","⠧","⠇","⠏"};

  size_t frame = 0;
  auto start = std::chrono::steady_clock::now();

  std::cout << Utils::Color::CYAN << message << " " << Utils::Color::RESET;

  while (std::chrono::steady_clock::now() - start <
         std::chrono::milliseconds(duration_ms)) {
    std::cout << "\b" << frames[frame] << std::flush;
    frame = (frame + 1) % frames.size();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }

  std::cout << "\b \n";
}

void UIManager::spinner(std::atomic<bool> &done) {
  const std::array<std::string_view, 10> frames = {
      "⠋","⠙","⠹","⠸","⠼",
      "⠴","⠦","⠧","⠇","⠏"};
  int frame = 0;

  while (!done) {
    std::cout << "\r" << frames[frame % 10] << std::flush;
    frame++;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
  std::cout << "\r \r" << std::flush;
}

// ---------------------------------------------------------------------------
// Discovery report
// ---------------------------------------------------------------------------

void UIManager::show_discovery_report(const DiscoveryLines &d) {
  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  Project Discovery" << Utils::Color::RESET
            << "\n";
  print_divider();

  std::cout << "  Type: " << d.project_type << "\n";
  std::cout << "  Size: " << d.source_file_count << " source files"
            << "\n";
  std::cout << "  Services: " << d.service_count << "\n";
  std::cout << "  Tests: " << (d.has_tests ? "present" : "none") << "\n";

  if (!d.ci_systems.empty()) {
    std::cout << "  CI: ";
    for (size_t i = 0; i < d.ci_systems.size(); i++) {
      if (i > 0)
        std::cout << ", ";
      std::cout << d.ci_systems[i];
    }
    std::cout << "\n";
  }

  if (!d.package_managers.empty()) {
    std::cout << "  Package managers:"
              << "\n";
    for (auto &pm : d.package_managers) {
      std::cout << "    " << Utils::Color::GREEN << "\u2713"
                << Utils::Color::RESET << " " << pm << "\n";
    }
  }

  if (!d.relevant_files.empty()) {
    std::cout << "\n  Relevant areas:"
              << "\n";
    for (auto &f : d.relevant_files) {
      std::cout << "    - " << f << "\n";
    }
  }

  if (!d.impact_areas.empty()) {
    std::cout << "\n  Potential impact:"
              << "\n";
    for (auto &a : d.impact_areas) {
      std::cout << "    - " << a << "\n";
    }
  }

  print_divider();
  std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Task plan display
// ---------------------------------------------------------------------------

void UIManager::show_task_plan(const std::vector<PlanTaskLine> &tasks) {
  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  Plan" << Utils::Color::RESET << "\n";
  print_divider();
  for (size_t i = 0; i < tasks.size(); i++) {
    std::cout << "  [" << (i + 1) << "] " << tasks[i].description;
    if (!tasks[i].file_ref.empty())
      std::cout << "  " << Utils::Color::DIM << "(" << tasks[i].file_ref << ")"
                << Utils::Color::RESET;
    std::cout << "\n";
  }
  print_divider();
  std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Execution trace
// ---------------------------------------------------------------------------

void UIManager::begin_execution(const std::string &title, int total_steps) {
  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  " << title << Utils::Color::RESET
            << " (" << total_steps << " steps)"
            << "\n";
  print_divider();
}

void UIManager::step_started(int step, const std::string &label) {
  std::cout << "  [" << step << "/] " << Utils::Color::YELLOW << "\u25CB"
            << Utils::Color::RESET << " " << label << "\n";
}

void UIManager::step_completed(int step, const std::string &label,
                                const std::string &detail) {
  std::cout << "  [" << step << "] " << Utils::Color::GREEN << "\u2713"
            << Utils::Color::RESET << " " << label;
  if (!detail.empty())
    std::cout << "  " << Utils::Color::DIM << detail << Utils::Color::RESET;
  std::cout << "\n";
}

void UIManager::step_failed(int step, const std::string &label,
                             const std::string &reason) {
  std::cout << "  [" << step << "] " << Utils::Color::RED << "\u2717"
            << Utils::Color::RESET << " " << label;
  if (!reason.empty())
    std::cout << "  " << Utils::Color::DIM << reason << Utils::Color::RESET;
  std::cout << "\n";
}

void UIManager::step_no_evidence(int step, const std::string &label,
                                  const std::string &detail) {
  std::cout << "  [" << step << "] " << Utils::Color::DIM << "\u25CB"
            << Utils::Color::RESET << " " << Utils::Color::DIM << label
            << Utils::Color::RESET;
  if (!detail.empty())
    std::cout << "  " << Utils::Color::DIM << detail << Utils::Color::RESET;
  std::cout << "\n";
}

void UIManager::end_execution(int succeeded, int failed) {
  print_divider();
  std::cout << "  " << succeeded << " completed";
  if (failed > 0)
    std::cout << ", " << failed << " failed";
  std::cout << "\n";
  print_divider();
  std::cout << "\n";
}

void UIManager::show_execution_summary(const ExecutionSummaryData &data) {
  std::cout << "  " << Utils::Color::BOLD << "Execution Summary"
            << Utils::Color::RESET << "\n";
  std::cout << "  " << Utils::Color::GREEN << "\u2713"
            << Utils::Color::RESET << " " << data.verified << " verified";
  if (data.not_executed > 0)
    std::cout << ",  " << Utils::Color::DIM << "\u25CB" << Utils::Color::RESET
              << " " << data.not_executed << " not executed";
  if (data.failed > 0)
    std::cout << ",  " << Utils::Color::RED << "\u2717" << Utils::Color::RESET
              << " " << data.failed << " failed";
  std::cout << "\n";

  if (!data.files_changed.empty()) {
    std::cout << "\n  Files changed:\n";
    for (auto &f : data.files_changed)
      std::cout << "    " << f << "\n";
  }

  if (!data.build_result.empty()) {
    bool ok = data.build_result.find("failed") == std::string::npos &&
              data.build_result.find("error") == std::string::npos;
    std::cout << "  Build: ";
    if (ok)
      std::cout << Utils::Color::GREEN << "\u2713" << Utils::Color::RESET;
    else
      std::cout << Utils::Color::RED << "\u2717" << Utils::Color::RESET;
    std::cout << "  " << data.build_result << "\n";
  }

  if (!data.test_result.empty()) {
    bool ok = data.test_result.find("failed") == std::string::npos &&
              data.test_result.find("FAILED") == std::string::npos;
    std::cout << "  Tests: ";
    if (ok)
      std::cout << Utils::Color::GREEN << "\u2713" << Utils::Color::RESET;
    else
      std::cout << Utils::Color::RED << "\u2717" << Utils::Color::RESET;
    std::cout << "  " << data.test_result << "\n";
  }

  print_divider();
  std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Tool visibility
// ---------------------------------------------------------------------------

void UIManager::show_tool_invocation(const std::string &tool,
                                     const std::string &args) {
  std::cout << "  " << Utils::Color::DIM << "Running: " << tool;
  if (!args.empty())
    std::cout << " " << args;
  std::cout << Utils::Color::RESET << "\n";
}

// ---------------------------------------------------------------------------
// Proposed changes preview (before apply)
// ---------------------------------------------------------------------------

void UIManager::show_preview(const std::vector<PlanTaskLine> &tasks) {
  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  Preview" << Utils::Color::RESET
            << "\n";
  print_divider();

  for (auto &t : tasks) {
    std::cout << "  " << Utils::Color::BOLD << t.description
              << Utils::Color::RESET << "\n";
    if (!t.file_ref.empty()) {
      std::cout << "    " << Utils::Color::GREEN << "+ " << t.file_ref
                << Utils::Color::RESET << "\n";
    }
  }
  print_divider();
  std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Apply prompt
// ---------------------------------------------------------------------------

bool UIManager::prompt_apply() {
  std::cout << "Apply these changes? [y/N] ";
  std::string line;
  std::getline(std::cin, line);
  return line == "y" || line == "Y";
}

// ---------------------------------------------------------------------------
// Change Preview display
// ---------------------------------------------------------------------------

void UIManager::show_change_preview(const ChangePreviewData &preview) {
  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  Change Preview" << Utils::Color::RESET
            << "\n";
  print_divider();

  if (!preview.files.empty()) {
    for (auto &f : preview.files) {
      std::cout << "  " << Utils::Color::BOLD << f.filename
                << Utils::Color::RESET << "\n";

      if (!f.diff_content.empty()) {
        // Print diff lines with +/- coloring
        std::istringstream stream(f.diff_content);
        std::string line;
        while (std::getline(stream, line)) {
          if (line.starts_with("+")) {
            std::cout << "    " << Utils::Color::GREEN << line
                      << Utils::Color::RESET << "\n";
          } else if (line.starts_with("-")) {
            std::cout << "    " << Utils::Color::RED << line
                      << Utils::Color::RESET << "\n";
          } else {
            std::cout << "    " << line << "\n";
          }
        }
      } else {
        std::cout << "    " << Utils::Color::DIM << "(no diff changes)"
                  << Utils::Color::RESET << "\n";
      }

      if (!f.build_result.empty()) {
        std::cout << "    Build: " << f.build_result << "\n";
      }
      if (!f.test_result.empty()) {
        std::cout << "    Tests: " << f.test_result << "\n";
      }
    }
  } else {
    std::cout << "  " << Utils::Color::DIM << "No files changed"
              << Utils::Color::RESET << "\n";
  }

  // Build summary
  if (!preview.build_result.empty()) {
    bool ok = preview.build_result.find("failed") == std::string::npos &&
              preview.build_result.find("error") == std::string::npos;
    std::cout << "  Build: ";
    if (ok) {
      std::cout << Utils::Color::GREEN << "\u2713" << Utils::Color::RESET;
    } else {
      std::cout << Utils::Color::RED << "\u2717" << Utils::Color::RESET;
    }
    std::cout << "  " << preview.build_result << "\n";
  }

  // Test summary
  if (!preview.test_result.empty()) {
    bool ok = preview.test_result.find("failed") == std::string::npos &&
              preview.test_result.find("FAILED") == std::string::npos;
    std::cout << "  Tests: ";
    if (ok) {
      std::cout << Utils::Color::GREEN << "\u2713" << Utils::Color::RESET;
    } else {
      std::cout << Utils::Color::RED << "\u2717" << Utils::Color::RESET;
    }
    std::cout << "  " << preview.test_result << "\n";
  }

  print_divider();
  std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Doctor report
// ---------------------------------------------------------------------------

void UIManager::show_doctor_report(const std::vector<CheckLine> &checks) {
  int passed = 0, failed = 0;
  for (auto &c : checks) {
    if (c.passed) passed++;
    else failed++;
  }

  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  Cursor Doctor" << Utils::Color::RESET << "\n";
  print_divider();

  for (auto &c : checks) {
    if (c.passed) {
      std::cout << "  " << Utils::Color::GREEN << "\u2713" << Utils::Color::RESET
                << " " << c.name;
      if (!c.details.empty())
        std::cout << "  " << Utils::Color::DIM << c.details << Utils::Color::RESET;
      std::cout << "\n";
    } else {
      std::cout << "  " << Utils::Color::RED << "\u2717" << Utils::Color::RESET
                << " " << c.name;
      if (!c.details.empty())
        std::cout << "  " << Utils::Color::DIM << c.details << Utils::Color::RESET;
      std::cout << "\n";
      if (!c.fix.empty())
        std::cout << "     " << Utils::Color::YELLOW << c.fix << Utils::Color::RESET << "\n";
    }
  }

  print_divider();
  std::cout << "  " << passed << " passed, " << failed << " failed"
            << "\n";
  print_divider();
  std::cout << "\n";
}

// ---------------------------------------------------------------------------
// Todo / progress display
// ---------------------------------------------------------------------------

void UIManager::show_todo_list(
    const std::vector<std::pair<std::string, bool>> &items) {
  std::cout << "\n";
  print_divider();
  std::cout << Utils::Color::BOLD << "  Tasks" << Utils::Color::RESET << "\n";
  print_divider();
  for (auto &item : items) {
    if (item.second) {
      std::cout << "  " << Utils::Color::GREEN << "[" << "\u2713" << "]"
                << Utils::Color::RESET << " " << item.first << "\n";
    } else {
      std::cout << "  [" << " " << "] " << item.first << "\n";
    }
  }
  print_divider();
  std::cout << "\n";
}

} // namespace Core
