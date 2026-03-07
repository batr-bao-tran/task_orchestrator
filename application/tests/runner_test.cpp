#include "runner/runner.hpp"

#include <gtest/gtest.h>

#include "config/config.hpp"

namespace {
namespace to = task_orchestrator;
TEST(RunnerTest, TwoTasksTwoAssignments) {
  to::app::WorkflowConfig cfg;
  cfg.id = "test";
  cfg.actors.push_back({.id = "r1", .type = "robot", .capacity = 2, .windows = {{.start = 0, .end = 1000}}});
  cfg.tasks.push_back(to::app::TaskConfig{.id = "t1",
                                          .requested_time = 0,
                                          .duration = 10,
                                          .deadline = 50,
                                          .allowed_actor_types = {"robot"},
                                          .phase_durations = {}});
  cfg.tasks.push_back(to::app::TaskConfig{.id = "t2",
                                          .requested_time = 0,
                                          .duration = 10,
                                          .deadline = 50,
                                          .allowed_actor_types = {"robot"},
                                          .phase_durations = {}});

  const to::app::RunResult result = to::app::run(cfg);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.assignments.size(), 2U);
  EXPECT_FALSE(result.capacity_issue);
  EXPECT_TRUE(result.unfulfilled_task_ids.empty());
}
}  // namespace
