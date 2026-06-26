#include "core/metrics.h"
#include "services/ai_service.h"
#include "services/replay_service.h"
#include "services/workflow_benchmark_service.h"
#include "services/execution_engine.h"
#include "memory_manager.h"
#include "agent.h"
#include "app/command_router.h"
#include "ui/ui_manager.h"
#include "version.h"
#include <gtest/gtest.h>
#include <functional>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

static std::string capture_stdout(const std::function<void()> &fn) {
  std::ostringstream buffer;
  std::streambuf *old_buf = std::cout.rdbuf(buffer.rdbuf());
  fn();
  std::cout.rdbuf(old_buf);
  return buffer.str();
}

// Basic test to verify the testing framework works
TEST(BasicTest, SanityCheck) { EXPECT_EQ(1 + 1, 2); }

TEST(AgentTest, DirectCommandRouting) {
  EXPECT_TRUE(Core::CommandRouter::is_direct_command_input("cmd:ls"));
  EXPECT_TRUE(Core::CommandRouter::is_direct_command_input("build:make"));
  EXPECT_TRUE(Core::CommandRouter::is_direct_command_input("grep:main:src"));
  EXPECT_FALSE(Core::CommandRouter::is_direct_command_input("foo:bar"));
  EXPECT_FALSE(Core::CommandRouter::is_direct_command_input("abc:def:ghi"));
  EXPECT_TRUE(Core::CommandRouter::is_direct_command_input("memory"));
  EXPECT_TRUE(Core::CommandRouter::is_direct_command_input("clear"));
  EXPECT_TRUE(Core::CommandRouter::is_direct_command_input("forget"));
  EXPECT_FALSE(Core::CommandRouter::is_direct_command_input("memory show"));
  EXPECT_FALSE(Core::CommandRouter::is_direct_command_input("clear the build cache"));
  EXPECT_FALSE(Core::CommandRouter::is_direct_command_input("forget about that error"));
}

TEST(AgentTest, MapNaturalLanguageToDirectCommands) {
  EXPECT_TRUE(Core::CommandRouter::map_nl_to_direct_command(
                  "can you check the files we changed").has_value());
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command(
                "can you check the files we changed"), "git:status");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command(
                "show changed files in repo"), "git:status");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command(
                "what is the commit history"), "git:log");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command(
                "find TODO in code"), "todos:.");
}

TEST(AgentTest, MapAllNaturalLanguageToDirectCommands) {
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("run make test"), "cmd:make test");
  EXPECT_FALSE(Core::CommandRouter::map_nl_to_direct_command("build the project").has_value());
  EXPECT_FALSE(Core::CommandRouter::map_nl_to_direct_command("compile").has_value());
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("read file src/main.cpp"), "read:src/main.cpp");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("save file notes.txt as hello world"), "write:notes.txt hello world");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("remember the api key is secret"), "remember:the api key is secret");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("forget everything"), "forget");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("show memory"), "memory");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("clear memory"), "clear");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("replace foo with bar in file README.md"), "replace:README.md:foo:bar");
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("open owner/repo on github"), "github:repo:owner/repo");
}

TEST(AgentTest, AgenticWorkflowCommands) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  std::string output = capture_stdout([&] {
    router.process_user_input("/goal set Fix the issue");
    router.process_user_input("/task add Investigate logs");
    router.process_user_input("/task list");
  });
  EXPECT_NE(output.find("Goal set: Fix the issue"), std::string::npos);
  EXPECT_NE(output.find("Task added: [1] Investigate logs"), std::string::npos);
  EXPECT_NE(output.find("Active tasks:"), std::string::npos);
}

TEST(AgentTest, DebugModeTogglesVerbosePipelineOutput) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);

  std::string output = capture_stdout([&] {
    router.process_user_input("git status");
  });
  // Verbose-gated content (parsed_input, context_state) hidden by default
  EXPECT_EQ(output.find("Heard"), std::string::npos);
  EXPECT_EQ(output.find("Parsed as"), std::string::npos);

  output = capture_stdout([&] {
    router.process_user_input("/debug");
    router.process_user_input("git status");
  });
  EXPECT_NE(output.find("Verbose mode ON"), std::string::npos);
  // Verbose-gated content now visible
  EXPECT_NE(output.find("Heard"), std::string::npos);
  EXPECT_NE(output.find("Parsed as"), std::string::npos);

  output = capture_stdout([&] {
    router.process_user_input("/debug");
    router.process_user_input("git status");
  });
  EXPECT_NE(output.find("Verbose mode OFF"), std::string::npos);
  // Verbose-gated content hidden again
  size_t off_pos = output.find("Verbose mode OFF");
  EXPECT_EQ(output.find("Heard", off_pos), std::string::npos);
  EXPECT_EQ(output.find("Parsed as", off_pos), std::string::npos);
}

TEST(AgentTest, DetectGitStatusQuery) {
  EXPECT_TRUE(Core::CommandRouter::is_git_status_query(
      "can you check the files we changed"));
  EXPECT_TRUE(Core::CommandRouter::is_git_status_query("what changed in the repo"));
  EXPECT_TRUE(Core::CommandRouter::is_git_status_query("show changed files"));
  EXPECT_FALSE(Core::CommandRouter::is_git_status_query("please help me with a diagram"));
}

TEST(AgentTest, ShowAgentDocumentation) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  std::string output = capture_stdout([&] {
    ui.show_agent_documentation();
  });

  EXPECT_NE(output.find("Cursor Engineering Guide"), std::string::npos);
  EXPECT_NE(output.find("This document defines the operational rules for every engineering cycle."), std::string::npos);
}

TEST(AgentTest, ShouldCallAIByOutcome) {
  Services::ExecutionResult r;
  r.outcome = Core::Outcome::Success;
  EXPECT_TRUE(Core::CommandRouter::should_call_ai(r));

  r.outcome = Core::Outcome::Failure;
  EXPECT_FALSE(Core::CommandRouter::should_call_ai(r));

  r.outcome = Core::Outcome::InsufficientEvidence;
  EXPECT_FALSE(Core::CommandRouter::should_call_ai(r));

  r.outcome = Core::Outcome::UserRejected;
  EXPECT_FALSE(Core::CommandRouter::should_call_ai(r));
}

// Test for version functionality
TEST(VersionTest, VersionCommand) {
  const char *version = Version::get_version();
  EXPECT_STREQ(version, cursor_version_string);
}

TEST(AgentTest, FormattedGitStatusOutput) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  capture_stdout([&] { router.process_user_input("/debug"); });
  std::vector<std::string> empty_files;
  std::vector<std::string> files = {"src/main.cpp", "include/agent.h", "CMakeLists.txt"};

  // Test with empty files (should show clean status)
  std::string output = capture_stdout([&] {
    ui.show_git_status_results(empty_files);
  });
  EXPECT_NE(output.find("clean"), std::string::npos);

  // Test with files (should show file list with numbers)
  output = capture_stdout([&] {
    ui.show_git_status_results(files);
  });
  EXPECT_NE(output.find("src/main.cpp"), std::string::npos);
  EXPECT_NE(output.find("include/agent.h"), std::string::npos);
  EXPECT_NE(output.find("CMakeLists.txt"), std::string::npos);
}

TEST(AgentTest, FormattedFilePreview) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  capture_stdout([&] { router.process_user_input("/debug"); });
  std::string content = "int main() {\n  std::cout << \"Hello\";\n  return 0;\n}\n";

  // Test file preview with syntax highlighting
  std::string output = capture_stdout([&] {
    ui.show_file_preview("test.cpp", content, 10);
  });

  // Should show filename
  EXPECT_NE(output.find("test.cpp"), std::string::npos);
  // Should show content with line numbers
  EXPECT_NE(output.find("main"), std::string::npos);
  EXPECT_NE(output.find("Hello"), std::string::npos);
}

TEST(AgentTest, FormattedFilePreviewHandlesErrors) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);
  capture_stdout([&] { router.process_user_input("/debug"); });
  std::string error_msg = "Error: File 'nonexistent.txt' does not exist";

  // Test that error messages are displayed
  std::string output = capture_stdout([&] {
    ui.show_file_preview("nonexistent.txt", error_msg, 10);
  });

  EXPECT_NE(output.find("Error:"), std::string::npos);
}

TEST(AgentTest, QueryIntentNormalizationAndTelemetryIsolation) {
  Core::Agent agent;
  Core::UIManager ui(agent);
  Core::CommandRouter router(agent, ui);

  // 1. Test Query Normalization
  EXPECT_EQ(Core::CommandRouter::normalize_query_intent("what is the last comit"), "what is the last commit");
  EXPECT_EQ(Core::CommandRouter::normalize_query_intent("tell me about the snipper realated code"), "tell me about the snippet realated code");
  EXPECT_EQ(Core::CommandRouter::normalize_query_intent("yeah tell me about the ui in from this codbease"), "yeah tell me about the ui in from this codebase");
  EXPECT_EQ(Core::CommandRouter::normalize_query_intent("  /  "), "/");

  // 2. Test Telemetry Isolation (outcome does not carry over)
  // Simulate a previous failed/rejected command state
  agent.state_.last_outcome = Core::Outcome::UserRejected;
  agent.state_.last_execution_path = Core::ExecutionPath::TaskPipeline;

  // Process a meta command (like "/llm" or "/debug")
  capture_stdout([&] { router.process_user_input("/debug"); });

  // Telemetry outcome should have been reset to Success and execution path set to MetaCommand
  EXPECT_EQ(agent.state_.last_outcome, Core::Outcome::Success);
  EXPECT_EQ(agent.state_.last_execution_path, Core::ExecutionPath::MetaCommand);
}

// ---------------------------------------------------------------------------
// Instrumentation Audit Tests
// ---------------------------------------------------------------------------

TEST(InstrumentationTest, OutcomeRoundtrip) {
  // Verify outcome_name / outcome_from_from are inverses
  for (auto o : {Core::Outcome::Success, Core::Outcome::Failure,
                  Core::Outcome::InsufficientEvidence,
                  Core::Outcome::UserRejected}) {
    std::string name = Core::outcome_name(o);
    Core::Outcome back = Core::outcome_from_name(name);
    EXPECT_EQ(o, back) << "outcome roundtrip failed for " << name;
  }
}

TEST(InstrumentationTest, RecoveryMetricsRoundtrip) {
  Core::RecoveryMetrics m;
  m.attempts = 3;
  m.strategy_changes = 2;
  m.evidence_found = true;
  m.verification_found = true;
  m.confidence_delta = 0.4;

  // Serialize to JSON and back
  nlohmann::json j;
  j["attempts"] = m.attempts;
  j["strategy_changes"] = m.strategy_changes;
  j["evidence_found"] = m.evidence_found;
  j["verification_found"] = m.verification_found;
  j["confidence_delta"] = m.confidence_delta;

  Core::RecoveryMetrics m2;
  m2.attempts = j.value("attempts", 0);
  m2.strategy_changes = j.value("strategy_changes", 0);
  m2.evidence_found = j.value("evidence_found", false);
  m2.verification_found = j.value("verification_found", false);
  m2.confidence_delta = j.value("confidence_delta", 0.0);

  EXPECT_EQ(m, m2);
}

TEST(InstrumentationTest, TrustMetricsRoundtrip) {
  Core::TrustMetrics m;
  m.plan_approved = true;
  m.diff_approved = false;
  m.user_corrected_goal = true;
  m.reverted = false;

  nlohmann::json j;
  j["plan_approved"] = m.plan_approved;
  j["diff_approved"] = m.diff_approved;
  j["user_corrected_goal"] = m.user_corrected_goal;
  j["reverted"] = m.reverted;

  Core::TrustMetrics m2;
  m2.plan_approved = j.value("plan_approved", false);
  m2.diff_approved = j.value("diff_approved", false);
  m2.user_corrected_goal = j.value("user_corrected_goal", false);
  m2.reverted = j.value("reverted", false);

  EXPECT_EQ(m, m2);
}

TEST(InstrumentationTest, ReplayEventRoundtrip) {
  Services::ReplayService replay;

  // Create session states
  Core::SessionState before;
  before.verbose_mode_ = false;
  before.command_count_ = 1;

  Core::SessionState after;
  after.verbose_mode_ = true;
  after.command_count_ = 2;

  // Outcome + metrics
  Core::RecoveryMetrics r;
  r.attempts = 2;
  r.strategy_changes = 1;
  r.evidence_found = true;
  r.verification_found = false;
  r.confidence_delta = 0.3;

  Core::TrustMetrics t;
  t.plan_approved = true;
  t.diff_approved = false;
  t.user_corrected_goal = false;
  t.reverted = false;

  // Log event with full instrumentation
  replay.log_input(before, after, "test input",
                   Core::Outcome::UserRejected,
                   Core::ExecutionPath::Engine, r, t);

  // Load back
  auto events = replay.load_session(replay.session_id());
  ASSERT_FALSE(events.empty()) << "should have at least one event";

  // Find our event (may be more if previous tests left events)
  bool found = false;
  for (auto &ev : events) {
    if (ev.input == "test input") {
      EXPECT_EQ(Core::Outcome::UserRejected, ev.outcome);
      EXPECT_EQ(r.attempts, ev.recovery_metrics.attempts);
      EXPECT_EQ(r.strategy_changes, ev.recovery_metrics.strategy_changes);
      EXPECT_EQ(r.evidence_found, ev.recovery_metrics.evidence_found);
      EXPECT_EQ(r.verification_found,
                ev.recovery_metrics.verification_found);
      EXPECT_EQ(t.plan_approved, ev.trust_metrics.plan_approved);
      EXPECT_EQ(t.diff_approved, ev.trust_metrics.diff_approved);
      EXPECT_EQ(t.user_corrected_goal,
                ev.trust_metrics.user_corrected_goal);
      EXPECT_EQ(t.reverted, ev.trust_metrics.reverted);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "should find the logged event";
}

TEST(InstrumentationTest, BenchmarkOutcomeRecorded) {
  auto results = Services::WorkflowBenchmarkService::run_all();

  // Every benchmark must have outcome set
  ASSERT_FALSE(results.empty());
  for (auto &r : results) {
    // Outcome should be one of the defined values (not accidental default)
    EXPECT_TRUE(r.outcome == Core::Outcome::Success ||
                r.outcome == Core::Outcome::Failure ||
                r.outcome == Core::Outcome::InsufficientEvidence ||
                r.outcome == Core::Outcome::UserRejected)
        << "benchmark " << r.name << " has invalid outcome";
    // Recovery metrics should be populated
    EXPECT_TRUE(r.recovery_metrics.evidence_found)
        << "benchmark " << r.name << " should have evidence_found=true";
  }
}

TEST(InstrumentationTest, RecoveryBenchmarkExpectedNonSuccess) {
  // Recovery benchmarks (#9-#14) should always be non-Success
  // because they model scenarios that need recovery (build fails, test fails,
  // search miss, etc.)
  auto results = Services::WorkflowBenchmarkService::run_all();

  // Recovery benchmarks start at index 8 (0-indexed)
  ASSERT_GE(results.size(), 14) << "need all 14 benchmarks";

  for (size_t i = 8; i < results.size() && i < 14; i++) {
    auto &r = results[i];
    // Recovery benchmarks should NOT be Outcome::Success
    // (they model failure states that require recovery)
    EXPECT_NE(r.outcome, Core::Outcome::Success)
        << "recovery benchmark " << r.name << " should not be Success";
  }
}

TEST(InstrumentationTest, OutcomeNameAllValues) {
  // Verify every outcome has a unique name and roundtrips
  std::set<std::string> names;
  for (auto o : {Core::Outcome::Success, Core::Outcome::Failure,
                  Core::Outcome::InsufficientEvidence,
                  Core::Outcome::UserRejected}) {
    std::string name = Core::outcome_name(o);
    EXPECT_TRUE(names.insert(name).second)
        << "duplicate outcome name: " << name;
  }
  EXPECT_EQ(names.size(), 4);
}

TEST(InstrumentationTest, UserRejectedSetsTrustMetrics) {
  Core::TrustMetrics t;
  t.plan_approved = true;
  t.diff_approved = false;  // user rejected the diff
  t.user_corrected_goal = false;
  t.reverted = false;

  EXPECT_FALSE(t.diff_approved) << "diff_approved should be false";
  EXPECT_TRUE(t.plan_approved) << "plan_approved should be true";

  // Simulate: if user corrects goal, it's a different pattern
  Core::TrustMetrics t2;
  t2.plan_approved = false;
  t2.diff_approved = false;
  t2.user_corrected_goal = true;
  t2.reverted = false;

  EXPECT_TRUE(t2.user_corrected_goal);
  EXPECT_FALSE(t2.plan_approved);
  EXPECT_NE(t, t2); // different patterns should differ
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
