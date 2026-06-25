#include "diagnostics/diagnostics.h"

#include "agent.h"
#include "app/command_router.h"
#include "core/trace_event.h"
#include "services/ai_service.h"
#include "services/command_service.h"
#include "services/discovery_service.h"
#include "services/execution_engine.h"
#include "services/file_service.h"
#include "services/find_service.h"
#include "services/symbol_service.h"
#include "services/web_service.h"
#include "ui/ui_manager.h"
#include "utils/config.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <vector>

namespace {

using json = nlohmann::json;

int check_prompt_assertions(const Services::ExecutionResult &res,
                            const json &expect,
                            const std::vector<Core::TraceEvent> &trace);
int check_command_assertions(const Services::StreamTelemetry &telemetry,
                             const std::string &output,
                             const json &expect);

} // anonymous namespace

void feed_consumer(TraceConsumer &consumer, const QueryResult &qr);

static bool is_opik_enabled() {
  std::string enable_val = Utils::Config::get_env_var("OPIK_ENABLE");
  if (!enable_val.empty()) {
    return enable_val == "true" || enable_val == "1";
  }
  std::string api_key = Utils::Config::get_env_var("OPIK_API_KEY");
  return !api_key.empty() && api_key != "your_opik_api_key_here";
}

class OpikConsumer : public TraceConsumer {
private:
  std::string prompt_;
  std::vector<json> trace_events_;
  std::chrono::steady_clock::time_point start_time_;
  std::string start_time_iso_;

public:
  void start_session(const std::string &prompt) override {
    prompt_ = prompt;
    start_time_ = std::chrono::steady_clock::now();
    start_time_iso_ = get_iso8601_timestamp();
    trace_events_.clear();
  }

  void handle_event(const Core::TraceEvent &e) override {
    json ev;
    ev["tool"] = e.tool;
    if (!e.args.empty()) {
      ev["args"] = e.args;
    }
    if (!e.files.empty()) {
      auto &f_arr = ev["files"];
      for (auto &f : e.files) {
        f_arr.push_back(f);
      }
    }
    trace_events_.push_back(ev);
  }

  void end_session(const Services::ExecutionResult &result) override {
    auto end_time = std::chrono::steady_clock::now();
    std::string end_time_iso = get_iso8601_timestamp();
    double duration = std::chrono::duration<double>(end_time - start_time_).count();

    if (!is_opik_enabled()) {
      return;
    }

    // Determine project name
    std::string project_name = Utils::Config::get_env_var("OPIK_PROJECT_NAME");
    std::string scenario_name = Utils::Config::get_env_var("OPIK_SCENARIO_NAME");

    if (project_name.empty()) {
      if (!scenario_name.empty()) {
        project_name = "Scenario Tests";
      } else {
        project_name = "Default Project";
      }
    }

    // Get Opik API URL
    std::string base_url = Utils::Config::get_env_var("OPIK_URL_OVERRIDE");
    if (base_url.empty()) {
      base_url = Utils::Config::get_env_var("OPIK_URL");
    }
    if (base_url.empty()) {
      base_url = "http://localhost:5173/api";
    }

    // Standardize url to remove trailing slash
    if (base_url.ends_with("/")) {
      base_url.pop_back();
    }

    std::string url = base_url + "/v1/private/traces";

    // Prepare JSON payload
    json j;
    j["name"] = !scenario_name.empty() ? scenario_name : "run_query";
    j["project_name"] = project_name;
    j["start_time"] = start_time_iso_;
    j["end_time"] = end_time_iso;

    j["input"] = {{"prompt", prompt_}};

    std::string outcome_str = Core::outcome_name(result.outcome);
    bool ai_called = Core::CommandRouter::should_call_ai(result);
    std::string goal_type_str = Services::ExecutionEngine::goal_type_name(
        static_cast<Services::ExecutionEngine::GoalType>(result.goal_type));

    j["output"] = {
        {"outcome", outcome_str},
        {"ai_called", ai_called},
        {"duration_seconds", duration},
        {"trace_events", trace_events_}
    };

    j["metadata"] = {
        {"goal_type", goal_type_str},
        {"confidence", result.confidence}
    };

    // Add tags
    std::vector<std::string> tags;
    if (!scenario_name.empty()) {
      tags.push_back("scenario-test");
      tags.push_back(scenario_name);
    }
    if (!tags.empty()) {
      j["tags"] = tags;
    }

    // Headers
    HeaderMap headers;
    std::string api_key = Utils::Config::get_env_var("OPIK_API_KEY");
    if (!api_key.empty()) {
      headers["Authorization"] = api_key;
    }
    std::string workspace = Utils::Config::get_env_var("OPIK_WORKSPACE");
    if (!workspace.empty()) {
      headers["Comet-Workspace"] = workspace;
    }
    headers["Content-Type"] = "application/json";

    auto resp = Services::WebService::post_json(url, j.dump(), headers);
    if (!resp.success) {
      std::cerr << "[Opik] Failed to upload trace: " << resp.error_message << "\n";
    }
  }

private:
  std::string get_iso8601_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    #if defined(_WIN32)
    struct tm gmt;
    gmtime_s(&gmt, &in_time_t);
    ss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%S");
    #else
    struct tm gmt;
    gmtime_r(&in_time_t, &gmt);
    ss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%S");
    #endif
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
  }
};

void upload_scenario_to_opik(
    const std::string &scenario_path,
    const std::string &type,
    const std::string &input_val,
    double duration,
    bool passed,
    const std::string &result_outcome,
    bool ai_called,
    const std::vector<Core::TraceEvent> &trace,
    const json &expect) {

  if (!is_opik_enabled()) {
    return;
  }

  std::string name = std::filesystem::path(scenario_path).filename().string();
  std::string project_name = Utils::Config::get_env_var("OPIK_PROJECT_NAME");
  if (project_name.empty()) {
    project_name = "Scenario Tests";
  }

  std::string base_url = Utils::Config::get_env_var("OPIK_URL_OVERRIDE");
  if (base_url.empty()) {
    base_url = Utils::Config::get_env_var("OPIK_URL");
  }
  if (base_url.empty()) {
    base_url = "http://localhost:5173/api";
  }
  if (base_url.ends_with("/")) {
    base_url.pop_back();
  }

  std::string url = base_url + "/v1/private/traces";

  // Format timestamps
  auto now = std::chrono::system_clock::now();
  auto start_time = now - std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::duration<double>(duration));
  
  auto get_iso = [](std::chrono::system_clock::time_point tp) -> std::string {
    auto in_time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    std::stringstream ss;
    #if defined(_WIN32)
    struct tm gmt;
    gmtime_s(&gmt, &in_time_t);
    ss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%S");
    #else
    struct tm gmt;
    gmtime_r(&in_time_t, &gmt);
    ss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%S");
    #endif
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
  };

  json j;
  j["name"] = name;
  j["project_name"] = project_name;
  j["start_time"] = get_iso(start_time);
  j["end_time"] = get_iso(now);

  if (type == "prompt") {
    j["input"] = {{"prompt", input_val}};
  } else {
    j["input"] = {{"command", input_val}};
  }

  json trace_events = json::array();
  for (auto &e : trace) {
    json ev;
    ev["tool"] = e.tool;
    if (!e.args.empty()) {
      ev["args"] = e.args;
    }
    if (!e.files.empty()) {
      auto &f_arr = ev["files"];
      for (auto &f : e.files) {
        f_arr.push_back(f);
      }
    }
    trace_events.push_back(ev);
  }

  j["output"] = {
      {"status", passed ? "PASS" : "FAIL"},
      {"outcome", result_outcome},
      {"ai_called", ai_called},
      {"duration_seconds", duration},
      {"trace_events", trace_events}
  };

  j["metadata"] = {
      {"scenario_path", scenario_path},
      {"type", type},
      {"expectations", expect}
  };

  j["tags"] = json::array({"scenario-test", name, passed ? "passed" : "failed"});

  HeaderMap headers;
  std::string api_key = Utils::Config::get_env_var("OPIK_API_KEY");
  if (!api_key.empty()) {
    headers["Authorization"] = api_key;
  }
  std::string workspace = Utils::Config::get_env_var("OPIK_WORKSPACE");
  if (!workspace.empty()) {
    headers["Comet-Workspace"] = workspace;
  }
  headers["Content-Type"] = "application/json";

  auto resp = Services::WebService::post_json(url, j.dump(), headers);
  if (!resp.success) {
    std::cerr << "[Opik] Failed to upload scenario trace: " << resp.error_message << "\n";
  }
}

// ===================================================================
// Shared execution path: one tool runner, always collects trace events
// ===================================================================

QueryResult run_query(const std::string &prompt) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  Services::ExecutionEngine engine;

  std::vector<Core::TraceEvent> trace;
  std::vector<Services::FileSearchResult> grep_results;
  std::string last_find;

  auto res = engine.execute(
      prompt,
      [&](const Services::ToolCall &tc) -> Services::ToolResult {
        Services::ToolResult tr;
        Core::TraceEvent ev;
        ev.type = Core::TraceEventType::ToolStarted;
        ev.tool = tc.tool;
        ev.args = tc.args;

        if (tc.tool == "grep") {
          auto r = Services::FileService::search_in_directory(
              ".", tc.args.empty() ? prompt : tc.args, "*");
          std::vector<Services::FileSearchResult> filtered;
          for (auto &x : r) {
            std::string p = x.file_path;
            std::replace(p.begin(), p.end(), '\\', '/');
            if (p.find("/scenarios/") != std::string::npos)
              continue;
            if (p.find("/build/") != std::string::npos)
              continue;
            if (p.find("/data/") != std::string::npos)
              continue;
            filtered.push_back(x);
          }
          grep_results = filtered;
          for (auto &x : filtered)
            ev.files.push_back(x.file_path);
          trace.push_back(ev);
          if (filtered.empty()) {
            tr.out = "no matches";
          } else {
            for (auto &x : filtered) {
              tr.out += x.file_path + ":" + std::to_string(x.line_number) + ": " +
                     x.line_content + "\n";
            }
          }
          return tr;
        }

        if (tc.tool == "find") {
          std::string term = tc.args;
          auto impl_pos = term.find(" --impl");
          bool impl_query = (impl_pos != std::string::npos);
          if (impl_query) term = term.substr(0, impl_pos);
          auto candidates = Services::directory_aware_find(term, impl_query);
          if (candidates.empty()) { tr.out = "no matches"; return tr; }
          last_find = candidates[0].path;
          for (auto &c : candidates) {
            tr.out += "CANDIDATE: " + c.path + " " + std::to_string(c.score) + " " + c.reason + "\n";
            ev.files.push_back(c.path);
          }
          tr.out += "SELECTED: " + candidates[0].path + "\n";
          tr.out += "REASON: " + candidates[0].reason + "\n";
          tr.out += "FILES:\n";
          for (auto &c : candidates)
            tr.out += c.path + "\n";
          trace.push_back(ev);
          return tr;
        }

        if (tc.tool == "references") {
          auto refs = Services::SymbolService::find_references(".", tc.args);
          if (refs.empty()) { tr.out = "no matches"; return tr; }
          last_find = refs[0].file;
          for (auto &r : refs) {
            ev.files.push_back(r.file);
          }
          trace.push_back(ev);
          tr.out = Services::SymbolService::format_references(refs);
          return tr;
        }

        if (tc.tool == "read") {
          std::vector<std::string> unique_files;
          std::set<std::string> seen;

          if (!tc.args.empty()) {
            std::istringstream ss(tc.args);
            std::string fname;
            while (ss >> fname) {
              if (seen.find(fname) == seen.end()) {
                seen.insert(fname);
                unique_files.push_back(fname);
              }
            }
          } else if (!last_find.empty()) {
            unique_files.push_back(last_find);
          } else {
            if (grep_results.empty()) {
              trace.push_back(ev);
              tr.out = "no files to read";
              return tr;
            }
            for (auto &x : grep_results) {
              if (seen.find(x.file_path) == seen.end()) {
                seen.insert(x.file_path);
                unique_files.push_back(x.file_path);
              }
            }
          }

          int count = 0;
          for (auto &f : unique_files) {
            if (count >= 5) break;
            ev.files.push_back(f);
            count++;
          }
          trace.push_back(ev);
          if (ev.files.empty()) {
            tr.out = "no files to read";
          } else {
            count = 0;
            for (auto &f : unique_files) {
              if (count >= 5) break;
              std::string content =
                  Services::FileService::read_file_range(f, 1, 30);
              tr.out += "--- " + f + " ---\n" + content.substr(0, 500) + "\n";
              count++;
            }
          }
          return tr;
        }

        if (tc.tool == "discovery") {
          trace.push_back(ev);
          auto d = Services::DiscoveryService::scan(".", prompt);
          tr.out = "Project: " + d.project_type;
          return tr;
        }

        if (tc.tool == "gh") {
          trace.push_back(ev);
          tr.out = Services::CommandService::execute("gh " + tc.args);
          return tr;
        }

        if (tc.tool == "git") {
          trace.push_back(ev);
          tr.out = Services::CommandService::execute("git " + tc.args);
          return tr;
        }

        if (tc.tool == "cmake" || tc.tool == "ctest") {
          trace.push_back(ev);
          tr.out = Services::CommandService::execute(tc.args);
          return tr;
        }

        trace.push_back(ev);
        tr.out = "unknown tool";
        return tr;
      },
      ui);

  QueryResult qr = {prompt, res, trace};
  if (is_opik_enabled()) {
    OpikConsumer opik;
    feed_consumer(opik, qr);
  }
  return qr;
}

// ===================================================================
// Renderers
// ===================================================================

void feed_consumer(TraceConsumer &consumer, const QueryResult &qr) {
  consumer.start_session(qr.prompt);
  for (auto &e : qr.trace) {
    consumer.handle_event(e);
  }
  consumer.end_session(qr.result);
}

class JsonConsumer : public TraceConsumer {
private:
  std::string prompt_;
  std::vector<Core::TraceEvent> trace_;
public:
  void start_session(const std::string &prompt) override {
    prompt_ = prompt;
  }
  void handle_event(const Core::TraceEvent &event) override {
    trace_.push_back(event);
  }
  void end_session(const Services::ExecutionResult &result) override {
    auto files = extract_files_examined(result.evidence.facts);

    json j;
    j["prompt"] = prompt_;
    j["goal_type"] = Services::ExecutionEngine::goal_type_name(
        static_cast<Services::ExecutionEngine::GoalType>(result.goal_type));
    j["outcome"] = Core::outcome_name(result.outcome);
    j["ai_called"] = Core::CommandRouter::should_call_ai(result);
    j["confidence"] = result.confidence;

    auto &files_arr = j["files_examined"];
    for (auto &f : files) {
      files_arr.push_back(f);
    }

    auto &tools_arr = j["tools"];
    std::vector<std::string> tools;
    for (auto &f : result.evidence.facts) {
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
  }
};

class TimelineConsumer : public TraceConsumer {
private:
  std::string prompt_;
  std::vector<std::string> event_logs_;
public:
  void start_session(const std::string &prompt) override {
    prompt_ = prompt;
  }
  void handle_event(const Core::TraceEvent &e) override {
    std::ostringstream ss;
    if (e.tool == "grep") {
      ss << "[search]\n";
      ss << "  tool:    grep\n";
      ss << "  query:   " << e.args << "\n";
      ss << "  matches: " << e.files.size() << " files\n";
      if (e.files.size() <= 15) {
        for (auto &f : e.files)
          ss << "    " << f << "\n";
      } else {
        for (int i = 0; i < 10 && i < (int)e.files.size(); i++)
          ss << "    " << e.files[i] << "\n";
        ss << "    ... (" << (e.files.size() - 10) << " more)\n";
      }
      ss << "\n";
    } else if (e.tool == "read") {
      ss << "[read]\n";
      if (!e.files.empty()) {
        ss << "  files:\n";
        for (auto &f : e.files)
          ss << "    " << f << "\n";
      } else {
        ss << "  files: none\n";
      }
      ss << "\n";
    } else if (e.tool == "discovery") {
      ss << "[discovery]\n\n";
    } else if (e.tool == "gh" || e.tool == "cmake" || e.tool == "ctest") {
      ss << "[" << e.tool << "]\n";
      ss << "  args: " << e.args << "\n\n";
    } else {
      ss << "[" << e.tool << "]\n\n";
    }
    event_logs_.push_back(ss.str());
  }
  void end_session(const Services::ExecutionResult &result) override {
    std::string route_name = Services::ExecutionEngine::goal_type_name(
        static_cast<Services::ExecutionEngine::GoalType>(result.goal_type));
    std::cout << "[route] " << route_name << "\n\n";

    for (auto &log : event_logs_) {
      std::cout << log;
    }

    std::cout << "[evidence]\n";
    std::cout << "  facts: " << result.evidence.facts.size() << " collected\n\n";

    std::cout << "[outcome]\n";
    std::cout << "  result: " << Core::outcome_name(result.outcome) << "\n\n";

    bool will_call_ai = Core::CommandRouter::should_call_ai(result);
    std::cout << "[ai]\n";
    if (will_call_ai) {
      std::cout << "  called: yes\n";
      if (!result.ai_response.empty())
        std::cout << "  response: " << result.ai_response.substr(0, 200) << "\n";
    } else {
      std::cout << "  called: no (outcome != success)\n";
    }
    std::cout << "\n";
  }
};

class TraceFileConsumer : public TraceConsumer {
private:
  std::string prompt_;
  std::string output_path_;
  json j_;
  bool success_{true};
public:
  TraceFileConsumer(const std::string &output_path) : output_path_(output_path) {}
  bool was_successful() const { return success_; }
  void start_session(const std::string &prompt) override {
    prompt_ = prompt;
  }
  void handle_event(const Core::TraceEvent &e) override {
    json ev;
    ev["tool"] = e.tool;
    if (e.tool == "grep") {
      ev["query"] = e.args.empty() ? prompt_ : e.args;
    }
    if (!e.files.empty()) {
      auto &f_arr = ev["files"];
      for (auto &f : e.files) {
        f_arr.push_back(f);
      }
    }
    j_["events"].push_back(ev);
  }
  void end_session(const Services::ExecutionResult &result) override {
    j_["outcome"] = Core::outcome_name(result.outcome);
    j_["ai_called"] = Core::CommandRouter::should_call_ai(result);

    std::ofstream ofs(output_path_);
    if (!ofs) {
      std::cerr << "Cannot write trace to " << output_path_ << "\n";
      success_ = false;
      return;
    }
    ofs << j_.dump(2) << std::endl;
    std::cout << "Trace written to " << output_path_ << "\n";
  }
};

class EvidenceExporterConsumer : public TraceConsumer {
private:
  std::string prompt_;
  std::string output_path_;
  bool success_{true};
public:
  EvidenceExporterConsumer(const std::string &output_path) : output_path_(output_path) {}
  bool was_successful() const { return success_; }
  void start_session(const std::string &prompt) override {
    prompt_ = prompt;
  }
  void handle_event(const Core::TraceEvent &) override {}
  void end_session(const Services::ExecutionResult &result) override {
    auto files = extract_files_examined(result.evidence.facts);

    json j;
    j["prompt"] = prompt_;
    j["goal_type"] = Services::ExecutionEngine::goal_type_name(
        static_cast<Services::ExecutionEngine::GoalType>(result.goal_type));
    j["outcome"] = Core::outcome_name(result.outcome);
    j["confidence"] = result.confidence;
    j["ai_called"] = Core::CommandRouter::should_call_ai(result);

    auto &facts_arr = j["evidence"]["facts"];
    for (auto &f : result.evidence.facts) {
      facts_arr.push_back(f);
    }

    auto &files_arr = j["evidence"]["files_examined"];
    for (auto &f : files) {
      files_arr.push_back(f);
    }

    std::vector<std::string> tools;
    for (auto &f : result.evidence.facts) {
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

    std::ofstream ofs(output_path_);
    if (!ofs) {
      std::cerr << "Cannot write evidence to " << output_path_ << "\n";
      success_ = false;
      return;
    }
    ofs << j.dump(2) << std::endl;
    std::cout << "Evidence exported to " << output_path_ << "\n";
  }
};

int render_json(const QueryResult &qr) {
  JsonConsumer consumer;
  feed_consumer(consumer, qr);
  return 0;
}

int render_timeline(const QueryResult &qr) {
  TimelineConsumer consumer;
  feed_consumer(consumer, qr);
  return 0;
}

int write_trace(const QueryResult &qr, const std::string &output_path) {
  TraceFileConsumer consumer(output_path);
  feed_consumer(consumer, qr);
  return consumer.was_successful() ? 0 : 1;
}

int export_evidence(const QueryResult &qr, const std::string &output_path) {
  EvidenceExporterConsumer consumer(output_path);
  feed_consumer(consumer, qr);
  return consumer.was_successful() ? 0 : 1;
}

// ===================================================================
// Wrappers (dispatch layer — each creates its own QueryResult)
// ===================================================================

int run_diagnostics(const std::string &prompt) {
  auto qr = run_query(prompt);

  json j;
  j["goal_type"] = Services::ExecutionEngine::goal_type_name(
      static_cast<Services::ExecutionEngine::GoalType>(qr.result.goal_type));
  j["outcome"] = Core::outcome_name(qr.result.outcome);
  j["confidence"] = qr.result.confidence;
  j["evidence_count"] = static_cast<int>(qr.result.evidence.facts.size());
  j["ai_called"] = Core::CommandRouter::should_call_ai(qr.result);

  std::vector<std::string> tools;
  for (auto &f : qr.result.evidence.facts) {
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
  return render_json(run_query(prompt));
}

int run_timeline(const std::string &prompt) {
  return render_timeline(run_query(prompt));
}

int run_trace_query(const std::string &prompt,
                    const std::string &output_path) {
  return write_trace(run_query(prompt), output_path);
}

int run_export_evidence(const std::string &prompt,
                        const std::string &output_path) {
  return export_evidence(run_query(prompt), output_path);
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

  // Handle suite format: root has a "queries" array of individual scenarios
  if (j.contains("queries") && j["queries"].is_array()) {
    auto &queries = j["queries"];
    for (auto &entry : queries) {
      std::string entry_id = entry.value("id", "?");
      if (!entry.contains("prompt") && !entry.contains("command")) {
        std::cerr << "FAIL " << scenario_path
                  << " entry '" << entry_id << "'  (missing 'prompt' or 'command')\n";
        return 1;
      }
    }
    return 0;
  }

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
    auto start_time = std::chrono::steady_clock::now();
    auto qr = run_query(prompt);
    int rc = check_prompt_assertions(qr.result, expect, qr.trace);
    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << (rc == 0 ? "PASS" : "FAIL") << "\n";

    upload_scenario_to_opik(
        scenario_path,
        "prompt",
        prompt,
        duration,
        rc == 0,
        Core::outcome_name(qr.result.outcome),
        Core::CommandRouter::should_call_ai(qr.result),
        qr.trace,
        expect
    );

    return rc;
  }

  if (is_command) {
    std::string command = j["command"];
#if defined(_WIN32)
    // Resolve `./build/bin/cursor-agent` to the correct local Windows path
    std::string exe_path;
    if (std::filesystem::exists("build/bin/Release/cursor-agent.exe")) {
      exe_path = "build\\bin\\Release\\cursor-agent.exe";
    } else if (std::filesystem::exists("build/bin/Debug/cursor-agent.exe")) {
      exe_path = "build\\bin\\Debug\\cursor-agent.exe";
    } else if (std::filesystem::exists("build/bin/cursor-agent.exe")) {
      exe_path = "build\\bin\\cursor-agent.exe";
    } else {
      exe_path = "build\\bin\\cursor-agent.exe"; // Fallback
    }

    size_t pos = 0;
    while ((pos = command.find("./build/bin/cursor-agent", pos)) != std::string::npos) {
      command.replace(pos, 25, exe_path);
      pos += exe_path.length();
    }

    pos = 0;
    while ((pos = command.find("echo \"/\"", pos)) != std::string::npos) {
      command.replace(pos, 8, "echo /");
      pos += 6;
    }
#endif

    std::string name = std::filesystem::path(scenario_path).filename().string();
#if defined(_WIN32)
    _putenv_s("OPIK_SCENARIO_NAME", name.c_str());
#else
    setenv("OPIK_SCENARIO_NAME", name.c_str(), 1);
#endif

    auto start_time = std::chrono::steady_clock::now();
    Services::StreamTelemetry telemetry;
    Utils::Config::load_environment();
    std::string output = Services::CommandService::execute_with_telemetry(
        command, telemetry);
    int rc = check_command_assertions(telemetry, output, expect);
    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << (rc == 0 ? "PASS" : "FAIL") << "\n";

#if defined(_WIN32)
    _putenv_s("OPIK_SCENARIO_NAME", "");
#else
    unsetenv("OPIK_SCENARIO_NAME");
#endif

    upload_scenario_to_opik(
        scenario_path,
        "command",
        command,
        duration,
        rc == 0,
        std::to_string(telemetry.exit_code),
        false,
        {},
        expect
    );

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

  auto qr = run_query(prompt);

  int rc = check_prompt_assertions(qr.result, expect, qr.trace);
  std::cout << (rc == 0 ? "PASS" : "FAIL") << "\n";
  return rc;
}

std::vector<std::string> extract_files_examined(
    const std::vector<std::string> &facts) {
  std::set<std::string> files;
  for (auto &f : facts) {
    if (f.starts_with("[grep ")) {
      size_t pos = f.find(']');
      if (pos == std::string::npos) continue;
      std::string content = f.substr(pos + 2);
      std::istringstream lines(content);
      std::string line;
      while (std::getline(lines, line)) {
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
          files.insert(line.substr(0, colon));
        }
      }
    } else if (f.starts_with("[read ") || f.starts_with("[read]")) {
      size_t pos = f.find(']');
      if (pos == std::string::npos) continue;
      std::string content = f.substr(pos + 2);
      std::istringstream lines(content);
      std::string line;
      while (std::getline(lines, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        std::string trimmed = line.substr(start);
        if (trimmed.starts_with("--- ") && trimmed.ends_with(" ---")) {
          std::string path = trimmed.substr(4, trimmed.size() - 8);
          if (!path.empty()) {
            files.insert(path);
          }
        }
      }
    }
  }
  return {files.begin(), files.end()};
}

// ===================================================================
// Anonymous-namespace helper definitions
// ===================================================================

namespace {

int check_prompt_assertions(const Services::ExecutionResult &res,
                            const json &expect,
                            const std::vector<Core::TraceEvent> &trace) {
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
                << "  actual: " << (actual_ai_called ? "true" : "false")
                << "\n";
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

  if (expect.contains("tools_used")) {
    auto required = expect["tools_used"];
    std::set<std::string> tools_in_trace;
    for (auto &e : trace)
      tools_in_trace.insert(e.tool);
    for (auto &req : required) {
      std::string t = req;
      if (tools_in_trace.find(t) == tools_in_trace.end()) {
        std::cout << "  expected tool \"" << t << "\" was not used\n";
        all_ok = false;
      }
    }
  }

  return all_ok ? 0 : 1;
}

int check_command_assertions(const Services::StreamTelemetry &telemetry,
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
                << "  actual: " << (telemetry.timed_out ? "true" : "false")
                << "\n";
      all_ok = false;
    }
  }

  if (expect.contains("output_contains")) {
    json needle = expect["output_contains"];
    bool found = false;
    if (needle.is_array()) {
      for (auto &n : needle) {
        if (output.find(n.get<std::string>()) != std::string::npos) {
          found = true;
          break;
        }
      }
    } else {
      if (output.find(needle.get<std::string>()) != std::string::npos) {
        found = true;
      }
    }
    if (!found) {
      std::cout << "  expected output contains one of: ";
      if (needle.is_array()) {
        for (auto &n : needle) std::cout << "\"" << n.get<std::string>() << "\" ";
      } else {
        std::cout << "\"" << needle.get<std::string>() << "\"";
      }
      std::cout << "\n";
      all_ok = false;
    }
  }

  return all_ok ? 0 : 1;
}

} // anonymous namespace
