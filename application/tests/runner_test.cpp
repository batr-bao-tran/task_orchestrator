#include "runner/runner.hpp"

#include <gtest/gtest.h>

#include "config/config.hpp"

using namespace task_orchestrator::app;

TEST(RunnerTest, TwoTasksTwoAssignments) {
  WorkflowConfig cfg;
  cfg.id = "test";
  cfg.actors.push_back({"r1", "robot", 2, {{0, 1000}}});
  cfg.tasks.push_back({"t1", 0, 10, 50, {"robot"}});
  cfg.tasks.push_back({"t2", 0, 10, 50, {"robot"}});

  RunResult result = run(cfg);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.assignments.size(), 2u);
  EXPECT_FALSE(result.capacity_issue);
  EXPECT_TRUE(result.unfulfilled_task_ids.empty());
}
