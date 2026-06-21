#include "services/workflow_benchmark_service.h"

#include "services/command_service.h"
#include "services/file_service.h"
#include "services/git_service.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace Services {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string make_temp_dir() {
  std::string dir = std::filesystem::temp_directory_path() /
                    "cursor_benchmark_XXXXXX";
  static const char chars[] =
      "abcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, (int)(sizeof(chars) - 2));
  for (size_t i = dir.find("XXXXXX"); i != std::string::npos &&
       i + 6 <= dir.size(); i = dir.find("XXXXXX")) {
    for (int j = 0; j < 6; j++)
      dir[i + j] = chars[dist(gen)];
  }
  std::filesystem::create_directories(dir);
  return dir;
}

static bool contains(const std::string &haystack,
                      const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string WorkflowBenchmarkService::create_fixture_dir(
    const std::string &name) {
  (void)name;
  return make_temp_dir();
}

bool WorkflowBenchmarkService::verify_file_contains(
    const std::string &path, const std::string &content) {
  if (!std::filesystem::exists(path)) return false;
  std::string file_content = Services::FileService::read_file(path);
  return contains(file_content, content);
}

bool WorkflowBenchmarkService::verify_build_passes(
    const std::string &dir) {
  std::string out = Services::CommandService::execute(
      "cmake -S " + dir + " -B " + dir + "/build 2>&1 && " +
      "cmake --build " + dir + "/build 2>&1");
  return !contains(out, "error") && !contains(out, "Error");
}

bool WorkflowBenchmarkService::verify_tests_pass(
    const std::string &dir) {
  std::string out = Services::CommandService::execute(
      "ctest --test-dir " + dir + "/build 2>&1");
  return contains(out, "100% tests passed");
}

void WorkflowBenchmarkService::cleanup_fixture(
    const std::string &dir) {
  std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Scenario 1: Fix failing GitHub Action
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_fix_failing_ci() {
  BenchmarkResult r;
  r.name = "fix_failing_ci";

  std::string dir = create_fixture_dir("fix_ci");
  std::filesystem::create_directories(dir + "/.github/workflows");

  // Create a workflow file with an intentional error (wrong runner)
  Services::FileService::write_file(
      dir + "/.github/workflows/ci.yml",
      "name: CI\n"
      "on: [push]\n"
      "jobs:\n"
      "  test:\n"
      "    runs-on: ubuntu-latest\n"
      "    steps:\n"
      "      - uses: actions/checkout@v4\n"
      "      - name: Run tests\n"
      "        run: nonexistent-command\n");

  // Create CMakeLists.txt that would work
  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(test_fixture LANGUAGES CXX)\n"
      "add_executable(test_app main.cpp)\n");

  Services::FileService::write_file(
      dir + "/main.cpp",
      "#include <iostream>\nint main() { std::cout << \"ok\\n\"; }\n");

  // Init git so agent can operate
  Services::CommandService::execute(
      "git init " + dir + " 2>&1");
  Services::FileService::write_file(dir + "/.gitignore", "build/\n");

  // Task: the CI workflow runs 'nonexistent-command'. Fix it to 'cmake --build .'
  // For now just check the fixture is valid
  r.passed = std::filesystem::exists(dir + "/.github/workflows/ci.yml") &&
             std::filesystem::exists(dir + "/CMakeLists.txt");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Success;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 2: Add new CLI command
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_add_cli_command() {
  BenchmarkResult r;
  r.name = "add_cli_command";

  std::string dir = create_fixture_dir("add_command");

  // Minimal CLI app with one command
  Services::FileService::write_file(
      dir + "/main.cpp",
      "#include <iostream>\n"
      "#include <string>\n"
      "int main(int argc, char **argv) {\n"
      "  if (argc < 2) { std::cout << \"usage: app <cmd>\\n\"; return 1; }\n"
      "  std::string cmd = argv[1];\n"
      "  if (cmd == \"hello\") std::cout << \"Hello!\\n\";\n"
      "  else std::cout << \"Unknown: \" << cmd << \"\\n\";\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(test_cli LANGUAGES CXX)\n"
      "add_executable(app main.cpp)\n");

  // Task: add a "version" command that prints "1.0.0"
  // Fixture validation only for now
  r.passed = std::filesystem::exists(dir + "/main.cpp");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Success;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 3: Refactor service API
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_refactor_service_api() {
  BenchmarkResult r;
  r.name = "refactor_service_api";

  std::string dir = create_fixture_dir("refactor");

  // A service with a method that needs renaming
  Services::FileService::write_file(
      dir + "/math_service.h",
      "#pragma once\n"
      "namespace Math {\n"
      "  int calculate(int a, int b);\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/math_service.cpp",
      "#include \"math_service.h\"\n"
      "namespace Math {\n"
      "  int calculate(int a, int b) { return a + b; }\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/main.cpp",
      "#include \"math_service.h\"\n"
      "#include <iostream>\n"
      "int main() {\n"
      "  std::cout << Math::calculate(2, 3) << \"\\n\";\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(refactor LANGUAGES CXX)\n"
      "add_executable(app main.cpp math_service.cpp)\n");

  // Task: rename calculate → add, update all references
  r.passed = std::filesystem::exists(dir + "/math_service.h");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Success;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 4: Add unit tests
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_add_unit_tests() {
  BenchmarkResult r;
  r.name = "add_unit_tests";

  std::string dir = create_fixture_dir("add_tests");

  // Simple library to test
  Services::FileService::write_file(
      dir + "/calculator.h",
      "#pragma once\n"
      "namespace Calc {\n"
      "  int add(int a, int b);\n"
      "  int multiply(int a, int b);\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/calculator.cpp",
      "#include \"calculator.h\"\n"
      "namespace Calc {\n"
      "  int add(int a, int b) { return a + b; }\n"
      "  int multiply(int a, int b) { return a * b; }\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/main.cpp", "int main() { return 0; }\n");

  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(calc LANGUAGES CXX)\n"
      "add_library(calc calculator.cpp)\n"
      "add_executable(app main.cpp)\n"
      "target_link_libraries(app PRIVATE calc)\n");

  // Task: add test file using a framework
  r.passed = std::filesystem::exists(dir + "/calculator.h");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Success;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 5: Investigate build failure
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_investigate_build_failure() {
  BenchmarkResult r;
  r.name = "investigate_build_failure";

  std::string dir = create_fixture_dir("build_fix");

  // Broken CMakeLists.txt (missing source file reference)
  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(broken LANGUAGES CXX)\n"
      "add_executable(app missing.cpp)\n");  // missing.cpp doesn't exist

  Services::FileService::write_file(
      dir + "/main.cpp", "int main() { return 0; }\n");

  // Task: the build fails because CMakeLists.txt references missing.cpp
  r.passed = std::filesystem::exists(dir + "/CMakeLists.txt");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Failure;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 6: Update dependency
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_update_dependency() {
  BenchmarkResult r;
  r.name = "update_dependency";

  std::string dir = create_fixture_dir("update_dep");

  // CMake project with a FetchContent dependency at an old version
  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(dep_test LANGUAGES CXX)\n"
      "include(FetchContent)\n"
      "FetchContent_Declare(\n"
      "  fmt\n"
      "  GIT_REPOSITORY https://github.com/fmtlib/fmt.git\n"
      "  GIT_TAG 9.1.0\n"
      ")\n"
      "FetchContent_MakeAvailable(fmt)\n"
      "add_executable(app main.cpp)\n"
      "target_link_libraries(app PRIVATE fmt::fmt)\n");

  Services::FileService::write_file(
      dir + "/main.cpp",
      "#include <fmt/core.h>\n"
      "int main() { fmt::print(\"hello\\n\"); }\n");

  // Task: update fmt dependency from 9.1.0 to 10.1.0
  r.passed = std::filesystem::exists(dir + "/CMakeLists.txt");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Success;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 7: Find authentication code
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_find_auth_code() {
  BenchmarkResult r;
  r.name = "find_auth_code";

  std::string dir = create_fixture_dir("find_auth");

  // Project with auth in multiple files
  Services::FileService::write_file(
      dir + "/auth.h",
      "#pragma once\n"
      "#include <string>\n"
      "namespace Auth {\n"
      "  std::string login(const std::string &user,\n"
      "                    const std::string &pass);\n"
      "  bool verify_token(const std::string &token);\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/auth.cpp",
      "#include \"auth.h\"\n"
      "namespace Auth {\n"
      "  std::string login(const std::string &u, const std::string &p) {\n"
      "    if (u == \"admin\" && p == \"secret\") return \"token-xyz\";\n"
      "    return \"\";\n"
      "  }\n"
      "  bool verify_token(const std::string &t) {\n"
      "    return t == \"token-xyz\";\n"
      "  }\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/server.cpp",
      "#include \"auth.h\"\n"
      "#include <iostream>\n"
      "void handle_request(const std::string &token) {\n"
      "  if (Auth::verify_token(token))\n"
      "    std::cout << \"authenticated\\n\";\n"
      "}\n");

  // Task: find where authentication is implemented and verified
  r.passed = std::filesystem::exists(dir + "/auth.h");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::InsufficientEvidence;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Scenario 8: Export conversation history
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_export_history() {
  BenchmarkResult r;
  r.name = "export_history";

  std::string dir = create_fixture_dir("export");

  // Simulate a conversation history file
  Services::FileService::write_file(
      dir + "/history.txt",
      "user: hello\n"
      "assistant: hi\n"
      "user: what is 2+2?\n"
      "assistant: 4\n"
      "user: create a file\n"
      "assistant: done\n");

  // Task: export conversation to JSON format
  r.passed = std::filesystem::exists(dir + "/history.txt");
  r.details = r.passed ? "fixture ready" : "fixture failed";
  r.score = r.passed ? 100 : 0;
  r.outcome = Core::Outcome::Success;
  r.recovery_metrics.evidence_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Recovery Scenario 9: Broken CMakeLists.txt
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_recover_broken_cmakelists() {
  BenchmarkResult r;
  r.name = "recover_broken_cmakelists";

  std::string dir = create_fixture_dir("broken_cmake");

  // CMakeLists.txt references missing.cpp which doesn't exist
  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(broken LANGUAGES CXX)\n"
      "add_executable(app missing.cpp main.cpp)\n");

  Services::FileService::write_file(
      dir + "/main.cpp", "int main() { return 0; }\n");

  // Valid fixture
  bool fixture_ok =
      std::filesystem::exists(dir + "/CMakeLists.txt") &&
      std::filesystem::exists(dir + "/main.cpp");

  r.passed = fixture_ok;
  r.details = fixture_ok ? "fixture ready (missing.cpp absent, build will fail)"
                         : "fixture failed";
  r.score = fixture_ok ? 100 : 0;
  r.build_ok = false; // build should fail
  r.outcome = Core::Outcome::Failure; // expected: build fails, agent should diagnose
  r.recovery_metrics.evidence_found = true;
  r.recovery_metrics.verification_found = false;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Recovery Scenario 10: Broken GitHub Action
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_recover_broken_github_action() {
  BenchmarkResult r;
  r.name = "recover_broken_github_action";

  std::string dir = create_fixture_dir("broken_action");
  std::filesystem::create_directories(dir + "/.github/workflows");

  // Workflow references a path that doesn't exist
  Services::FileService::write_file(
      dir + "/.github/workflows/ci.yml",
      "name: CI\n"
      "on: [push]\n"
      "jobs:\n"
      "  build:\n"
      "    runs-on: ubuntu-latest\n"
      "    steps:\n"
      "      - uses: actions/checkout@v4\n"
      "      - name: Build\n"
      "        run: cmake --build nonexistent_dir\n");

  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(fix_ci LANGUAGES CXX)\n"
      "add_executable(app main.cpp)\n");

  Services::FileService::write_file(
      dir + "/main.cpp", "#include <iostream>\nint main() { return 0; }\n");

  Services::CommandService::execute("git init " + dir + " 2>&1");

  bool fixture_ok =
      std::filesystem::exists(dir + "/.github/workflows/ci.yml") &&
      std::filesystem::exists(dir + "/CMakeLists.txt");

  r.passed = fixture_ok;
  r.details = fixture_ok ? "fixture ready (ci.yml has wrong build path)"
                         : "fixture failed";
  r.score = fixture_ok ? 100 : 0;
  r.outcome = Core::Outcome::InsufficientEvidence; // agent needs to locate + fix path
  r.recovery_metrics.evidence_found = true;
  r.recovery_metrics.verification_found = false;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Recovery Scenario 11: Search miss - authentication code
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_search_miss_authentication() {
  BenchmarkResult r;
  r.name = "search_miss_authentication";

  std::string dir = create_fixture_dir("search_auth");

  // Auth implementation uses unconventional naming
  Services::FileService::write_file(
      dir + "/security.h",
      "#pragma once\n"
      "#include <string>\n"
      "namespace Sec {\n"
      "  std::string validate(const std::string &token);\n"
      "  bool check_access(const std::string &user);\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/security.cpp",
      "#include \"security.h\"\n"
      "namespace Sec {\n"
      "  std::string validate(const std::string &t) {\n"
      "    if (t == \"valid-token\") return \"ok\";\n"
      "    return \"denied\";\n"
      "  }\n"
      "  bool check_access(const std::string &u) {\n"
      "    return u == \"admin\";\n"
      "  }\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/api_handler.cpp",
      "#include \"security.h\"\n"
      "#include <iostream>\n"
      "void handle_api_request(const std::string &token) {\n"
      "  auto result = Sec::validate(token);\n"
      "  std::cout << \"Auth: \" << result << \"\\n\";\n"
      "}\n");

  // No file named 'auth' - agent must search by content, not name
  bool fixture_ok = std::filesystem::exists(dir + "/security.h") &&
                    std::filesystem::exists(dir + "/security.cpp") &&
                    std::filesystem::exists(dir + "/api_handler.cpp");

  r.passed = fixture_ok;
  r.details = fixture_ok
                  ? "fixture ready (auth code in security.h, no 'auth' filename)"
                  : "fixture failed";
  r.score = fixture_ok ? 100 : 0;
  r.outcome = Core::Outcome::InsufficientEvidence; // agent must expand search terms
  r.recovery_metrics.evidence_found = true;
  r.recovery_metrics.verification_found = false;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Recovery Scenario 12: Failing unit test
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_recover_failing_test() {
  BenchmarkResult r;
  r.name = "recover_failing_test";

  std::string dir = create_fixture_dir("fix_test");

  // Calculator with intentional bug
  Services::FileService::write_file(
      dir + "/calculator.h",
      "#pragma once\n"
      "namespace Calc {\n"
      "  int add(int a, int b);\n"
      "  int subtract(int a, int b);\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/calculator.cpp",
      "#include \"calculator.h\"\n"
      "namespace Calc {\n"
      "  int add(int a, int b) { return a + b + 1; }\n" // BUG: off-by-one
      "  int subtract(int a, int b) { return a - b; }\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/test_calculator.cpp",
      "#include \"calculator.h\"\n"
      "#include <cassert>\n"
      "int main() {\n"
      "  assert(Calc::add(2, 3) == 5);\n"  // will fail: returns 6
      "  assert(Calc::subtract(5, 3) == 2);\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(test_fix LANGUAGES CXX)\n"
      "add_executable(test_calc test_calculator.cpp calculator.cpp)\n"
      "enable_testing()\n"
      "add_test(NAME calc_test COMMAND test_calc)\n");

  bool build_ok = verify_build_passes(dir);
  r.build_ok = build_ok;

  if (build_ok) {
    bool tests_ok = verify_tests_pass(dir);
    r.test_ok = tests_ok;
    r.details = "fixture ready, test expected to fail";
    r.passed = true;
    r.score = 100;
    r.outcome = Core::Outcome::Failure; // test will fail - agent must diagnose
  } else {
    r.details = "fixture build failed";
    r.passed = false;
    r.score = 0;
    r.outcome = Core::Outcome::Failure;
  }

  r.recovery_metrics.evidence_found = true;
  r.recovery_metrics.verification_found = true;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Recovery Scenario 13: Missing dependency
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_missing_dependency() {
  BenchmarkResult r;
  r.name = "missing_dependency";

  std::string dir = create_fixture_dir("missing_dep");

  // CMakeLists.txt references a library not declared as a dependency
  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(missing_dep LANGUAGES CXX)\n"
      "add_executable(app main.cpp)\n"
      "target_link_libraries(app PRIVATE ssl)\n"); // ssl target not defined

  Services::FileService::write_file(
      dir + "/main.cpp",
      "#include <iostream>\n"
      "int main() { std::cout << \"hello\\n\"; }\n");

  // Also a package.json with a missing dependency
  Services::FileService::write_file(
      dir + "/package.json",
      "{\n"
      "  \"name\": \"test-app\",\n"
      "  \"dependencies\": {\n"
      "    \"nonexistent-package\": \"^1.0.0\"\n"
      "  }\n"
      "}\n");

  bool fixture_ok = std::filesystem::exists(dir + "/CMakeLists.txt") &&
                    std::filesystem::exists(dir + "/package.json");

  r.passed = fixture_ok;
  r.details = fixture_ok ? "fixture ready (missing deps in cmake + package.json)"
                         : "fixture failed";
  r.score = fixture_ok ? 100 : 0;
  r.outcome = Core::Outcome::InsufficientEvidence; // agent must detect + suggest fix
  r.recovery_metrics.evidence_found = true;
  r.recovery_metrics.verification_found = false;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Recovery Scenario 14: Misnamed config file
// ---------------------------------------------------------------------------

BenchmarkResult WorkflowBenchmarkService::scenario_misnamed_config() {
  BenchmarkResult r;
  r.name = "misnamed_config";

  std::string dir = create_fixture_dir("wrong_config");

  // Config file with a different name than expected
  Services::FileService::write_file(
      dir + "/setup.ini",
      "[database]\n"
      "host = localhost\n"
      "port = 5432\n"
      "name = myapp\n"
      "user = admin\n"
      "password = secret\n"
      "\n"
      "[server]\n"
      "port = 8080\n"
      "debug = true\n");

  // Code that tries to read the config from a wrong name
  Services::FileService::write_file(
      dir + "/config_reader.cpp",
      "#include <fstream>\n"
      "#include <iostream>\n"
      "#include <string>\n"
      "int main() {\n"
      "  std::ifstream f(\"config.ini\");\n" // wrong name!
      "  if (!f.is_open()) {\n"
      "    std::cerr << \"config.ini not found\\n\";\n"
      "    return 1;\n"
      "  }\n"
      "  std::cout << \"config loaded\\n\";\n"
      "}\n");

  Services::FileService::write_file(
      dir + "/CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.14)\n"
      "project(config_app LANGUAGES CXX)\n"
      "add_executable(app config_reader.cpp)\n");

  bool fixture_ok = std::filesystem::exists(dir + "/setup.ini") &&
                    std::filesystem::exists(dir + "/config_reader.cpp") &&
                    std::filesystem::exists(dir + "/CMakeLists.txt");

  r.passed = fixture_ok;
  r.details = fixture_ok
                  ? "fixture ready (config file named setup.ini, code reads config.ini)"
                  : "fixture failed";
  r.score = fixture_ok ? 100 : 0;
  r.outcome = Core::Outcome::Failure; // code won't find the file
  r.recovery_metrics.evidence_found = true;
  r.recovery_metrics.verification_found = false;

  cleanup_fixture(dir);
  return r;
}

// ---------------------------------------------------------------------------
// Run all
// ---------------------------------------------------------------------------

std::vector<BenchmarkResult> WorkflowBenchmarkService::run_all() {
  std::vector<BenchmarkResult> results;
  results.push_back(scenario_fix_failing_ci());
  results.push_back(scenario_add_cli_command());
  results.push_back(scenario_refactor_service_api());
  results.push_back(scenario_add_unit_tests());
  results.push_back(scenario_investigate_build_failure());
  results.push_back(scenario_update_dependency());
  results.push_back(scenario_find_auth_code());
  results.push_back(scenario_export_history());
  // Recovery benchmarks (9-14)
  results.push_back(scenario_recover_broken_cmakelists());
  results.push_back(scenario_recover_broken_github_action());
  results.push_back(scenario_search_miss_authentication());
  results.push_back(scenario_recover_failing_test());
  results.push_back(scenario_missing_dependency());
  results.push_back(scenario_misnamed_config());
  return results;
}

} // namespace Services
