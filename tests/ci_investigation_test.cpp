#include "services/ci_investigation_service.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace Services;

class CiInvestigationTest : public ::testing::Test {
protected:
  const std::string build_failure_json = R"json(
{
  "jobs": [
    {
      "name": "build (ubuntu, release)",
      "conclusion": "failure",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "Run build", "conclusion": "failure"},
        {"name": "Upload artifacts", "conclusion": "skipped"}
      ]
    },
    {
      "name": "lint",
      "conclusion": "success",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "Run clang-tidy", "conclusion": "success"}
      ]
    }
  ]
}
)json";

  const std::string test_failure_json = R"json(
{
  "jobs": [
    {
      "name": "test (ubuntu, 20.04)",
      "conclusion": "failure",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "Build tests", "conclusion": "success"},
        {"name": "Run tests", "conclusion": "failure"}
      ]
    }
  ]
}
)json";

  const std::string infra_failure_json = R"json(
{
  "jobs": [
    {
      "name": "e2e",
      "conclusion": "failure",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "Run docker/setup-buildx-action@v4", "conclusion": "failure"},
        {"name": "Run e2e tests", "conclusion": "cancelled"}
      ]
    }
  ]
}
)json";

  const std::string multi_failure_json = R"json(
{
  "jobs": [
    {
      "name": "build (windows, release)",
      "conclusion": "failure",
      "steps": [
        {"name": "Generate project files", "conclusion": "success"},
        {"name": "build", "conclusion": "failure"}
      ]
    },
    {
      "name": "build (ubuntu, release)",
      "conclusion": "failure",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "cmake", "conclusion": "success"},
        {"name": "make", "conclusion": "failure"}
      ]
    },
    {
      "name": "lint",
      "conclusion": "success",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "Run clang-format", "conclusion": "success"}
      ]
    }
  ]
}
)json";

  const std::string all_success_json = R"json(
{
  "jobs": [
    {
      "name": "build",
      "conclusion": "success",
      "steps": [
        {"name": "Checkout", "conclusion": "success"},
        {"name": "cmake", "conclusion": "success"},
        {"name": "make", "conclusion": "success"}
      ]
    },
    {
      "name": "test",
      "conclusion": "success",
      "steps": [
        {"name": "Run tests", "conclusion": "success"}
      ]
    }
  ]
}
)json";

  const std::string no_steps_json = R"json(
{
  "jobs": [
    {
      "name": "build",
      "conclusion": "failure"
    }
  ]
}
)json";

  const long long test_run_id = 12345678;
};

TEST_F(CiInvestigationTest, BuildFailure) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      build_failure_json, test_run_id);

  ASSERT_EQ(1u, failures.size());
  EXPECT_EQ("build (ubuntu, release)", failures[0].job_name);
  EXPECT_EQ("Run build", failures[0].step_name);
}

TEST_F(CiInvestigationTest, TestFailure) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      test_failure_json, test_run_id);

  ASSERT_EQ(1u, failures.size());
  EXPECT_EQ("test (ubuntu, 20.04)", failures[0].job_name);
  EXPECT_EQ("Run tests", failures[0].step_name);
}

TEST_F(CiInvestigationTest, InfrastructureFailure) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      infra_failure_json, test_run_id);

  ASSERT_EQ(1u, failures.size());
  EXPECT_EQ("e2e", failures[0].job_name);
  EXPECT_EQ("Run docker/setup-buildx-action@v4", failures[0].step_name);
}

TEST_F(CiInvestigationTest, MultipleJobFailures) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      multi_failure_json, test_run_id);

  ASSERT_EQ(2u, failures.size());
  EXPECT_EQ("build (windows, release)", failures[0].job_name);
  EXPECT_EQ("build", failures[0].step_name);
  EXPECT_EQ("build (ubuntu, release)", failures[1].job_name);
  EXPECT_EQ("make", failures[1].step_name);
}

TEST_F(CiInvestigationTest, AllSuccessReturnsEmpty) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      all_success_json, test_run_id);
  EXPECT_TRUE(failures.empty());
}

TEST_F(CiInvestigationTest, JobWithNoStepsArray) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      no_steps_json, test_run_id);

  ASSERT_EQ(1u, failures.size());
  EXPECT_EQ("build", failures[0].job_name);
  EXPECT_TRUE(failures[0].step_name.empty());
}

TEST_F(CiInvestigationTest, EmptyJsonReturnsEmpty) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      "", test_run_id);
  EXPECT_TRUE(failures.empty());
}

TEST_F(CiInvestigationTest, MalformedJsonReturnsEmpty) {
  auto failures = CiInvestigationService::parse_failed_steps_json(
      "this is not json", test_run_id);
  EXPECT_TRUE(failures.empty());
}
