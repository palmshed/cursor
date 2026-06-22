#pragma once

#include "core/trace_event.h"
#include "services/execution_engine.h"
#include <string>
#include <vector>

// == Shared execution path ==

struct QueryResult {
  std::string prompt;
  Services::ExecutionResult result;
  std::vector<Core::TraceEvent> trace;
};

QueryResult run_query(const std::string &prompt);

// == Trace Consumer Abstraction ==

class TraceConsumer {
public:
  virtual ~TraceConsumer() = default;
  virtual void start_session(const std::string &prompt) = 0;
  virtual void handle_event(const Core::TraceEvent &event) = 0;
  virtual void end_session(const Services::ExecutionResult &result) = 0;
};

// == Renderers (consume QueryResult) ==

// --diagnostics / --json output
int render_json(const QueryResult &qr);

// --timeline output
int render_timeline(const QueryResult &qr);

// --trace output (write trace.json)
int write_trace(const QueryResult &qr, const std::string &output_path);

// --export-evidence output
int export_evidence(const QueryResult &qr, const std::string &output_path);

// == Legacy thin wrappers (dispatch layer) ==

int run_diagnostics(const std::string &prompt);
int run_json_query(const std::string &prompt);
int run_trace_query(const std::string &prompt,
                    const std::string &output_path);
int run_timeline(const std::string &prompt);
int run_export_evidence(const std::string &prompt,
                        const std::string &output_path);

// --stream-report mode: run a command and record streaming telemetry
int run_stream_report(const std::string &command,
                      const std::string &output_path);

// --scenario mode: run a scenario JSON file and verify expected outcome
int run_scenario(const std::string &scenario_path);
int run_scenario_prompt(const std::string &prompt,
                        const std::string &expected_outcome,
                        bool expected_ai_called);

// Extract file paths from evidence facts
std::vector<std::string> extract_files_examined(
    const std::vector<std::string> &facts);
