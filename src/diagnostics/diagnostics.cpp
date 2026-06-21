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
#include <vector>

namespace {
using json = nlohmann::json;
} // namespace

int run_diagnostics(const std::string &prompt) {
  // Non-interactive single prompt diagnostics.
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);

  Services::ExecutionEngine engine;
  Services::ExecutionResult res;

  // Execute one engine loop using a tool runner that maps tool calls to concrete
  // local operations (same spirit as the original diagnostics block, but isolated here).
  res = engine.execute(
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
