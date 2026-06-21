#include "services/self_test_service.h"

#include "app/command_router.h"
#include "services/command_service.h"
#include "services/file_service.h"
#include "services/git_service.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace Services {

std::string SelfTestService::sandbox_dir() {
  std::string dir =
      std::filesystem::temp_directory_path() / "cursor_selftest_XXXXXX";
  // mkdtemp replacement
  static const char chars[] =
      "abcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
  for (size_t i = dir.find("XXXXXX"); i != std::string::npos && i + 6 <= dir.size();
       i = dir.find("XXXXXX")) {
    for (int j = 0; j < 6; j++)
      dir[i + j] = chars[dist(gen)];
  }
  std::filesystem::create_directories(dir);
  return dir;
}

bool SelfTestService::expect_contains(const std::string &haystack,
                                       const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------
// Workflow scenarios
// ---------------------------------------------------------------------------

ScenarioResult SelfTestService::test_file_create_read_write() {
  ScenarioResult r;
  r.name = "file create/read/write";

  std::string dir = sandbox_dir();
  std::string path = dir + "/test.txt";

  // Write
  std::string write_result = Services::FileService::write_file(path, "hello world");
  if (write_result.empty()) {
    r.passed = false;
    r.details = "write failed";
    std::filesystem::remove_all(dir);
    return r;
  }

  // Read
  std::string content = Services::FileService::read_file(path);
  if (content != "hello world") {
    r.passed = false;
    r.details = "read mismatch: '" + content + "'";
    std::filesystem::remove_all(dir);
    return r;
  }

  // Replace
  auto edit_result = Services::FileService::replace_text_in_file(path, "world", "cursor");
  if (!edit_result.success) {
    r.passed = false;
    r.details = "replace failed: " + edit_result.message;
    std::filesystem::remove_all(dir);
    return r;
  }

  content = Services::FileService::read_file(path);
  if (content != "hello cursor") {
    r.passed = false;
    r.details = "replace verify failed: '" + content + "'";
    std::filesystem::remove_all(dir);
    return r;
  }

  std::filesystem::remove_all(dir);
  r.passed = true;
  return r;
}

ScenarioResult SelfTestService::test_file_search() {
  ScenarioResult r;
  r.name = "file search (grep)";

  std::string dir = sandbox_dir();
  std::string path = dir + "/search.txt";
  Services::FileService::write_file(path, "unique_sentinel_abc123");

  auto results =
      Services::FileService::search_in_directory(dir, "unique_sentinel");
  bool found = false;
  for (const auto &res : results) {
    if (res.line_content.find("unique_sentinel") != std::string::npos)
      found = true;
  }

  std::filesystem::remove_all(dir);
  r.passed = found;
  r.details = found ? "pattern found" : "pattern not found";
  return r;
}

ScenarioResult SelfTestService::test_git_workflow() {
  ScenarioResult r;
  r.name = "git workflow";

  std::string dir = sandbox_dir();

  // Init repo
  std::string init_out = Services::CommandService::execute(
      "git init " + dir + " 2>&1");
  if (init_out.find("error") != std::string::npos && init_out.find("already") == std::string::npos) {
    r.passed = false;
    r.details = "git init failed: " + init_out;
    std::filesystem::remove_all(dir);
    return r;
  }

  // Create file and commit (with dummy user config for environments without global git config)
  Services::FileService::write_file(dir + "/readme.md", "# Test");
  Services::CommandService::execute(
      "git -C " + dir + " config user.email test@test.com 2>&1");
  Services::CommandService::execute(
      "git -C " + dir + " config user.name Tester 2>&1");
  Services::CommandService::execute(
      "git -C " + dir + " add . 2>&1");
  Services::CommandService::execute(
      "git -C " + dir + " commit -m \"init\" 2>&1");

  // Check git log via direct command
  std::string log = Services::CommandService::execute(
      "git -C " + dir + " log --oneline 2>&1");
  if (!expect_contains(log, "init")) {
    r.passed = false;
    r.details = "commit not in log: " + log;
    std::filesystem::remove_all(dir);
    return r;
  }

  // Check status (clean) via direct command
  std::string status = Services::CommandService::execute(
      "git -C " + dir + " status --porcelain 2>&1");
  r.passed = status.empty() || status.find("Exit code:") != std::string::npos == false;
  r.details = r.passed ? "clean repo" : "unexpected dirty: " + status;
  std::filesystem::remove_all(dir);
  return r;
}

ScenarioResult SelfTestService::test_shell_execution() {
  ScenarioResult r;
  r.name = "shell execution";

  std::string out = Services::CommandService::execute("echo selftest_ok");
  r.passed = expect_contains(out, "selftest_ok");
  r.details = r.passed ? "echo OK" : "echo failed: " + out;
  return r;
}

// ---------------------------------------------------------------------------
// Agent quality scenarios
// ---------------------------------------------------------------------------

ScenarioResult SelfTestService::test_codebase_query_classification() {
  ScenarioResult r;
  r.name = "codebase query classification";

  int total = 0, passed = 0;

  // Should be classified as codebase queries
  struct { std::string input; bool expected; } cases[] = {
    {"how does auth work in this project", true},
    {"where is the replay implementation", true},
    {"how are files saved", true},
    {"show me the command router", true},
    {"what files handle authentication", true},
    {"find where config is loaded", true},
    {"how does session run work", true},
    {"what is the difference between X and Y", false},
    {"how do I install python", false},
    {"explain the concept of AI", false},
    {"how does one write a loop in c++", false},
    {"hello", false},
  };

  for (auto &c : cases) {
    total++;
    bool result = Core::CommandRouter::is_codebase_query(c.input);
    if (result == c.expected) passed++;
  }

  r.passed = (passed == total);
  r.details = std::to_string(passed) + "/" + std::to_string(total) + " correct";
  return r;
}

ScenarioResult SelfTestService::test_git_status_detection() {
  ScenarioResult r;
  r.name = "git status detection";

  int total = 0, passed = 0;

  struct { std::string input; bool expected; } cases[] = {
    {"what files are modified", false},     // not in exact-match triggers
    {"what has changed", true},             // literal trigger match
    {"check git status", true},             // "git status" substring
    {"show me uncommitted changes", false}, // not in exact-match triggers
    {"how do I check git log", false},
    {"create a new file", false},
  };

  for (auto &c : cases) {
    total++;
    bool result = Core::CommandRouter::is_git_status_query(c.input);
    if (result == c.expected) passed++;
  }

  r.passed = (passed == total);
  r.details = std::to_string(passed) + "/" + std::to_string(total) + " correct";
  return r;
}

ScenarioResult SelfTestService::test_direct_command_routing() {
  ScenarioResult r;
  r.name = "direct command routing";

  int total = 0, passed = 0;

  struct { std::string input; bool expected; } cases[] = {
    {"grep:foo bar", true},
    {"read:src/main.cpp", true},
    {"write:test.txt", true},
    {"search:hello world", true},
    {"git:status", true},
    {"how are you", false},
    {"/help", false},
    {"/replay list", false},
    {"!ls", false},
  };

  for (auto &c : cases) {
    total++;
    bool result = Core::CommandRouter::is_direct_command_input(c.input);
    if (result == c.expected) passed++;
  }

  r.passed = (passed == total);
  r.details = std::to_string(passed) + "/" + std::to_string(total) + " correct";
  return r;
}

ScenarioResult SelfTestService::test_nl_command_mapping() {
  ScenarioResult r;
  r.name = "NL command mapping";

  int total = 0, passed = 0;

  struct { std::string input; bool should_map; } cases[] = {
    {"search for foo", false},              // needs code/repo/project context
    {"find bar in files", true},            // "find" + "in files"
    {"tell me about this project", false},  // no mapping pattern matches
    {"what components are there", true},    // "components" triggers mapping
    {"show todos", true},                   // "todos" triggers mapping
    {"how are you", false},
    {"hello world", false},
  };

  for (auto &c : cases) {
    total++;
    auto mapped = Core::CommandRouter::map_nl_to_direct_command(c.input);
    bool result = mapped.has_value();
    if (result == c.should_map) passed++;
  }

  r.passed = (passed == total);
  r.details = std::to_string(passed) + "/" + std::to_string(total) + " correct";
  return r;
}

// ---------------------------------------------------------------------------
// Run all
// ---------------------------------------------------------------------------

std::vector<ScenarioResult> SelfTestService::run_all_scenarios() {
  std::vector<ScenarioResult> results;
  results.push_back(test_file_create_read_write());
  results.push_back(test_file_search());
  results.push_back(test_git_workflow());
  results.push_back(test_shell_execution());
  results.push_back(test_codebase_query_classification());
  results.push_back(test_git_status_detection());
  results.push_back(test_direct_command_routing());
  results.push_back(test_nl_command_mapping());
  return results;
}

} // namespace Services
