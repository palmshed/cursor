#include "services/ai_service.h"
#include "memory_manager.h"
#include "agent.h"
#include "app/command_router.h"
#include "ui/ui_manager.h"
#include "version.h"
#include <gtest/gtest.h>
#include <functional>
#include <iostream>
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
  EXPECT_EQ(*Core::CommandRouter::map_nl_to_direct_command("build the project"), "build:make");
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
  EXPECT_EQ(output.find("INPUT RECEIVED"), std::string::npos);
  EXPECT_EQ(output.find("Input classification"), std::string::npos);

  output = capture_stdout([&] {
    router.process_user_input("/debug");
    router.process_user_input("git status");
  });
  EXPECT_NE(output.find("Verbose mode ON"), std::string::npos);
  EXPECT_NE(output.find("INPUT RECEIVED"), std::string::npos);
  EXPECT_NE(output.find("Input classification"), std::string::npos);

  output = capture_stdout([&] {
    router.process_user_input("/debug");
    router.process_user_input("git status");
  });
  EXPECT_NE(output.find("Verbose mode OFF"), std::string::npos);
  size_t off_pos = output.find("Verbose mode OFF");
  EXPECT_EQ(output.find("INPUT RECEIVED", off_pos), std::string::npos);
  EXPECT_EQ(output.find("Input classification", off_pos), std::string::npos);
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

  EXPECT_NE(output.find("Agents Architecture Guide"), std::string::npos);
  EXPECT_NE(output.find("This document describes the current system architecture and rules."), std::string::npos);
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
