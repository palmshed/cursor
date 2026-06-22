#include "diagnostics/diagnostics.h"

#include "agent.h"
#include "app/command_router.h"
#include "services/command_service.h"
#include "services/discovery_service.h"
#include "services/execution_engine.h"
#include "services/file_service.h"
#include "ui/ui_manager.h"
#include "utils/config.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <vector>

namespace {
using json = nlohmann::json;

struct TraceEvent {
  std::string tool;
  std::string args;
  std::vector<std::string> files;
};

// Shared tool runner used by all engine invocations.
// Tracks grep results to support read operations (mimics command_router behavior).
Services::ExecutionResult run_engine_once(
    const std::string &prompt,
    Services::ExecutionEngine &engine,
    Core::UIManager &ui) {

  std::vector<Services::FileSearchResult> last_grep_results;

  return engine.execute(
      prompt,
      [&](const Services::ToolCall &tc) -> std::string {
        if (tc.tool == "grep") {
          auto r = Services::FileService::search_in_directory(
              ".", tc.args.empty() ? prompt : tc.args, "*");
          // Filter out scenario files to prevent test strings from contaminating results.
          std::vector<Services::FileSearchResult> filtered;
          for (auto &x : r) {
            if (x.file_path.find("/scenarios/") != std::string::npos)
              continue;
            filtered.push_back(x);
          }
          last_grep_results = filtered;
          if (filtered.empty())
            return "no matches";
          std::string out;
          for (auto &x : filtered) {
            out += x.file_path + ":" + std::to_string(x.line_number) + ": " +
                   x.line_content + "\n";
          }
          return out;
        }

        if (tc.tool == "read") {
          if (last_grep_results.empty())
            return "no files to read";
          std::set<std::string> unique_files;
          for (auto &x : last_grep_results)
            unique_files.insert(x.file_path);
          std::string out;
          int count = 0;
          for (auto &f : unique_files) {
            if (count >= 5) break;
            std::string content = Services::FileService::read_file_range(f, 1, 30);
            out += "--- " + f + " ---\n" + content.substr(0, 500) + "\n";
            count++;
          }
          return out;
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

int run_trace_query(const std::string &prompt,
                    const std::string &output_path) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  Services::ExecutionEngine engine;

  std::vector<TraceEvent> trace;
  // Shared grep results for read operations (mimics command_router behavior).
  std::vector<Services::FileSearchResult> trace_grep_results;

  auto res = engine.execute(
      prompt,
      [&](const Services::ToolCall &tc) -> std::string {
        TraceEvent ev;
        ev.tool = tc.tool;
        ev.args = tc.args;

        if (tc.tool == "grep") {
          auto r = Services::FileService::search_in_directory(
              ".", tc.args.empty() ? prompt : tc.args, "*");
          // Filter out scenario files to prevent test strings from contaminating results.
          std::vector<Services::FileSearchResult> filtered;
          for (auto &x : r) {
            if (x.file_path.find("/scenarios/") != std::string::npos)
              continue;
            filtered.push_back(x);
          }
          trace_grep_results = filtered;
          for (auto &x : filtered) {
            ev.files.push_back(x.file_path);
          }
          trace.push_back(ev);
          if (filtered.empty()) return "no matches";
          std::string out;
          for (auto &x : filtered) {
            out += x.file_path + ":" + std::to_string(x.line_number) + ": " +
                   x.line_content + "\n";
          }
          return out;
        }

        if (tc.tool == "read") {
          if (trace_grep_results.empty()) {
            trace.push_back(ev);
            return "no files to read";
          }
          std::set<std::string> unique_files;
          for (auto &x : trace_grep_results)
            unique_files.insert(x.file_path);
          int count = 0;
          for (auto &f : unique_files) {
            if (count >= 5) break;
            ev.files.push_back(f);
            count++;
          }
          trace.push_back(ev);
          if (ev.files.empty()) return "no files to read";
          std::string out;
          count = 0;
          for (auto &f : unique_files) {
            if (count >= 5) break;
            std::string content = Services::FileService::read_file_range(f, 1, 30);
            out += "--- " + f + " ---\n" + content.substr(0, 500) + "\n";
            count++;
          }
          return out;
        }

        if (tc.tool == "discovery") {
          trace.push_back(ev);
          auto d = Services::DiscoveryService::scan(".", prompt);
          return "Project: " + d.project_type;
        }

        if (tc.tool == "gh") {
          trace.push_back(ev);
          return Services::CommandService::execute("gh " + tc.args);
        }

        if (tc.tool == "cmake" || tc.tool == "ctest") {
          trace.push_back(ev);
          return Services::CommandService::execute(tc.args);
        }

        trace.push_back(ev);
        return "unknown tool";
      },
      ui);

  json j;
  auto &evts = j["events"];
  for (auto &e : trace) {
    json ev;
    ev["tool"] = e.tool;
    if (e.tool == "grep") {
      ev["query"] = e.args.empty() ? prompt : e.args;
    }
    if (!e.files.empty()) {
      auto &f_arr = ev["files"];
      for (auto &f : e.files) {
        f_arr.push_back(f);
      }
    }
    evts.push_back(ev);
  }
  j["outcome"] = Core::outcome_name(res.outcome);
  j["ai_called"] = Core::CommandRouter::should_call_ai(res);

  std::ofstream ofs(output_path);
  if (!ofs) {
    std::cerr << "Cannot write trace to " << output_path << "\n";
    return 1;
  }
  ofs << j.dump(2) << std::endl;
  std::cout << "Trace written to " << output_path << "\n";
  return 0;
}

int run_stream_report(const std::string &command,
                      const std::string &output_path) {
  Services::StreamTelemetry telemetry;
  Utils::Config::load_environment();
  std::string output = Services::CommandService::execute_with_telemetry(
      command, telemetry);

  json j;
  j["command"] = telemetry.command;
  j["started_at"] = telemetry.started_at;
  if (telemetry.first_output_at > 0)
    j["first_output_at"] = telemetry.first_output_at;
  if (telemetry.last_output_at > 0)
    j["last_output_at"] = telemetry.last_output_at;
  j["completed_at"] = telemetry.completed_at;
  j["lines_streamed"] = telemetry.lines_streamed;
  j["exit_code"] = telemetry.exit_code;
  j["timed_out"] = telemetry.timed_out;
  j["output_preview"] = output.substr(0, 200);

  std::ofstream ofs(output_path);
  if (!ofs) {
    std::cerr << "Cannot write stream report to " << output_path << "\n";
    return 1;
  }
  ofs << j.dump(2) << std::endl;
  std::cout << "Stream report written to " << output_path << "\n";
  return telemetry.exit_code == 0 ? 0 : 1;
}

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

int run_export_evidence(const std::string &prompt,
                        const std::string &output_path) {
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
  j["confidence"] = res.confidence;
  j["ai_called"] = Core::CommandRouter::should_call_ai(res);

  auto &facts_arr = j["evidence"]["facts"];
  for (auto &f : res.evidence.facts) {
    facts_arr.push_back(f);
  }

  auto &files_arr = j["evidence"]["files_examined"];
  for (auto &f : files) {
    files_arr.push_back(f);
  }

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
  auto &tools_arr = j["evidence"]["tools"];
  for (auto &t : tools) {
    tools_arr.push_back(t);
  }

  std::ofstream ofs(output_path);
  if (!ofs) {
    std::cerr << "Cannot write evidence to " << output_path << "\n";
    return 1;
  }
  ofs << j.dump(2) << std::endl;
  std::cout << "Evidence exported to " << output_path << "\n";
  return 0;
}

static int check_prompt_assertions(
    const Services::ExecutionResult &res,
    const json &expect) {

  std::string actual_outcome = Core::outcome_name(res.outcome);
  bool actual_ai_called = Core::CommandRouter::should_call_ai(res);
  auto files = extract_files_examined(res.evidence.facts);

  bool all_ok = true;

  if (expect.contains("outcome")) {
    std::string expected_outcome = expect["outcome"];
    if (actual_outcome != expected_outcome) {
      std::cout << "  expected outcome: " << expected_outcome
                << "  actual: " << actual_outcome << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("ai_called")) {
    bool expected_ai = expect["ai_called"];
    if (actual_ai_called != expected_ai) {
      std::cout << "  expected ai_called: " << (expected_ai ? "true" : "false")
                << "  actual: " << (actual_ai_called ? "true" : "false") << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("files_examined_min")) {
    int min_files = expect["files_examined_min"];
    int actual_files = static_cast<int>(files.size());
    if (actual_files < min_files) {
      std::cout << "  expected files_examined >= " << min_files
                << "  actual: " << actual_files << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("evidence_nonempty")) {
    bool expected_nonempty = expect["evidence_nonempty"];
    bool actual_nonempty = !res.evidence.facts.empty();
    if (actual_nonempty != expected_nonempty) {
      std::cout << "  expected evidence_nonempty: "
                << (expected_nonempty ? "true" : "false")
                << "  actual: " << (actual_nonempty ? "true" : "false") << "\n";
      all_ok = false;
    }
  }

  return all_ok ? 0 : 1;
}

static int check_command_assertions(
    const Services::StreamTelemetry &telemetry,
    const std::string &output,
    const json &expect) {

  bool all_ok = true;

  if (expect.contains("exit_code")) {
    int expected_code = expect["exit_code"];
    if (telemetry.exit_code != expected_code) {
      std::cout << "  expected exit_code: " << expected_code
                << "  actual: " << telemetry.exit_code << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("lines_streamed_min")) {
    int min_lines = expect["lines_streamed_min"];
    if (telemetry.lines_streamed < min_lines) {
      std::cout << "  expected lines_streamed >= " << min_lines
                << "  actual: " << telemetry.lines_streamed << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("timed_out")) {
    bool expected_timeout = expect["timed_out"];
    if (telemetry.timed_out != expected_timeout) {
      std::cout << "  expected timed_out: "
                << (expected_timeout ? "true" : "false")
                << "  actual: " << (telemetry.timed_out ? "true" : "false") << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("output_contains")) {
    std::string needle = expect["output_contains"];
    if (output.find(needle) == std::string::npos) {
      std::cout << "  expected output contains: \"" << needle << "\"\n";
      all_ok = false;
    }
  }

  return all_ok ? 0 : 1;
}

int run_scenario(const std::string &scenario_path) {
  std::ifstream ifs(scenario_path);
  if (!ifs) {
    std::cerr << "FAIL " << scenario_path << "  (cannot open)\n";
    return 1;
  }
  json j;
  try {
    ifs >> j;
  } catch (...) {
    std::cerr << "FAIL " << scenario_path << "  (invalid JSON)\n";
    return 1;
  }

  // Determine scenario type
  bool is_prompt = j.contains("prompt");
  bool is_command = j.contains("command");

  if (!is_prompt && !is_command) {
    std::cerr << "FAIL " << scenario_path
              << "  (missing 'prompt' or 'command')\n";
    return 1;
  }

  json expect = j.value("expect", json::object());

  if (is_prompt) {
    std::string prompt = j["prompt"];
    Core::Agent agent;
    Core::UIManager ui(agent);
    Core::CommandRouter router(agent, ui);
    Services::ExecutionEngine engine;
    auto res = run_engine_once(prompt, engine, ui);
    int rc = check_prompt_assertions(res, expect);
    if (rc == 0) {
      std::cout << "PASS" << "\n";
    } else {
      std::cout << "FAIL" << "\n";
    }
    return rc;
  }

  if (is_command) {
    std::string command = j["command"];
    Services::StreamTelemetry telemetry;
    Utils::Config::load_environment();
    std::string output = Services::CommandService::execute_with_telemetry(
        command, telemetry);
    int rc = check_command_assertions(telemetry, output, expect);
    if (rc == 0) {
      std::cout << "PASS" << "\n";
    } else {
      std::cout << "FAIL" << "\n";
    }
    return rc;
  }

  return 1;
}

int run_scenario_prompt(const std::string &prompt,
                        const std::string &expected_outcome,
                        bool expected_ai_called) {
  json expect;
  expect["outcome"] = expected_outcome;
  expect["ai_called"] = expected_ai_called;

  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  Services::ExecutionEngine engine;
  auto res = run_engine_once(prompt, engine, ui);

  int rc = check_prompt_assertions(res, expect);
  if (rc == 0) {
    std::cout << "PASS" << "\n";
  } else {
    std::cout << "FAIL" << "\n";
  }
  return rc;
}

