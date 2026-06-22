#pragma once

#include <string>
#include <vector>

int run_diagnostics(const std::string &prompt);

// --json mode: single-prompt JSON diagnostics with files_examined
int run_json_query(const std::string &prompt);

// Extract file paths from evidence facts
std::vector<std::string> extract_files_examined(
    const std::vector<std::string> &facts);
