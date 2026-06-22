#include "diagnostics/diagnostics.h"

#include "agent.h"
#include "app/command_router.h"
#include "services/command_service.h"
#include "services/discovery_service.h"
#include "services/execution_engine.h"
#include "services/file_service.h"
#include "ui/ui_manager.h"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <vector>

namespace {
using json = nlohmann::json;

// Shared tool runner used by both run_diagnostics and run_json_query.
Services::ExecutionResult run_engine_once(
    const std::string &prompt,
    Services::ExecutionEngine &engine,
    Core::UIManager &ui) {

  return engine.execute(
      prompt,
      [&](const Services::ToolCall &tc) -> std::string {
        if (tc.tool == "grep") {
          auto r = Services::FileService::search_in_directory(
              ".", tc.args.empty() ? prompt : tc.args, "*");
          if (r.empty())
            return "no matches";
          std::string out;
          for (auto &x : r) {
            out += x.file_path + ":" + std::to_string(x.line_number) + ": " +
                   x.line_content + "\n";
          }
          return out;
        }

        if (tc.tool == "read") {
          return "no files to read";
        }

        if (tc.tool == "discovery") {
          auto d = Services::DiscoveryService::scan(".", prompt);
          return "Project: " + d.project_type;
        }

        if (tc.tool == "gh") {
          return Services::CommandService::execute("gh " + tc.args);
        }

        if (tc.tool == "cmake") {
          return Services::CommandService::execute(tc.args);
        }

        if (tc.tool == "ctest") {
          return Services::CommandService::execute(tc.args);
        }

        return "unknown tool";
      },
      ui);
}

// Extract file paths from grep evidence facts.
// Facts look like: "[grep <term>:results] file:line: content ..."
void extract_from_fact(const std::string &fact, std::set<std::string> &files) {
  if (fact.starts_with("[grep ")) {
    size_t pos = fact.find(']');
    if (pos == std::string::npos) return;
    std::string content = fact.substr(pos + 2);
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        files.insert(line.substr(0, colon));
      }
    }
  }
}

} // namespace

std::vector<std::string> extract_files_examined(
    const std::vector<std::string> &facts) {
  std::set<std::string> files;
  for (auto &f : facts) {
    extract_from_fact(f, files);
  }
  return {files.begin(), files.end()};
}

int run_diagnostics(const std::string &prompt) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  Services::ExecutionEngine engine;

  auto res = run_engine_once(prompt, engine, ui);

  json j;
  j["goal_type"] =
      Services::ExecutionEngine::goal_type_name(static_cast<Services::ExecutionEngine::GoalType>(
          res.goal_type));
  j["outcome"] = Core::outcome_name(res.outcome);
  j["confidence"] = res.confidence;
  j["evidence_count"] = static_cast<int>(res.evidence.facts.size());
  j["ai_called"] = Core::CommandRouter::should_call_ai(res);

  std::vector<std::string> tools;
  for (auto &f : res.evidence.facts) {
    if (f.find("grep") != std::string::npos &&
        std::find(tools.begin(), tools.end(), "grep") == tools.end()) {
      tools.push_back("grep");
    }
    if (f.find("read") != std::string::npos &&
        std::find(tools.begin(), tools.end(), "read") == tools.end()) {
      tools.push_back("read");
    }
  }
  j["tools"] = tools;

  std::cout << j.dump(2) << std::endl;
  return 0;
}

int run_json_query(const std::string &prompt) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  Services::ExecutionEngine engine;

  auto res = run_engine_once(prompt, engine, ui);

  auto files = extract_files_examined(res.evidence.facts);

  json j;
  j["prompt"] = prompt;
  j["goal_type"] =
      Services::ExecutionEngine::goal_type_name(static_cast<Services::ExecutionEngine::GoalType>(
          res.goal_type));
  j["outcome"] = Core::outcome_name(res.outcome);
  j["ai_called"] = Core::CommandRouter::should_call_ai(res);
  j["confidence"] = res.confidence;

  auto &files_arr = j["files_examined"];
  for (auto &f : files) {
    files_arr.push_back(f);
  }

  auto &tools_arr = j["tools"];
  std::vector<std::string> tools;
  for (auto &f : res.evidence.facts) {
    if (f.find("grep") != std::string::npos &&
        std::find(tools.begin(), tools.end(), "grep") == tools.end()) {
      tools.push_back("grep");
    }
    if (f.find("read") != std::string::npos &&
        std::find(tools.begin(), tools.end(), "read") == tools.end()) {
      tools.push_back("read");
    }
  }
  for (auto &t : tools) {
    tools_arr.push_back(t);
  }

  std::cout << j.dump(2) << std::endl;
  return 0;
}
