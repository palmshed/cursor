#pragma once

#include <string>
#include <vector>

int run_diagnostics(const std::string &prompt);

// --json mode: single-prompt JSON diagnostics with files_examined
int run_json_query(const std::string &prompt);

// --trace mode: run with tool tracing, write trace.json
int run_trace_query(const std::string &prompt,
                    const std::string &output_path);

// Extract file paths from evidence facts
std::vector<std::string> extract_files_examined(
    const std::vector<std::string> &facts);
