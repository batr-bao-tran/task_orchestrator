#include <gtest/gtest.h>

#include "config/config.hpp"
#include "runner/runner.hpp"

namespace {
namespace to = task_orchestrator;

TEST(ScenarioWarehouseTest, SimpleThreeTasks) {
  const std::string content = R"(
id: sc
actors:
  - id: r1
    type: robot
    capacity: 2
    windows: [{ start: 0, end: 1000 }]
  - id: r2
    type: robot
    capacity: 1
    windows: [{ start: 0, end: 1000 }]
tasks:
  - id: o1
    requested_time: 0
    duration: 30
    deadline: 100
    allowed_actor_types: [robot]
  - id: o2
    requested_time: 10
    duration: 20
    deadline: 80
    allowed_actor_types: [robot]
  - id: o3
    requested_time: 5
    duration: 25
    deadline: 90
    allowed_actor_types: [robot]
)";
  const to::app::WorkflowConfig config = to::app::load_config_from_string(content);
  const to::app::RunResult result = to::app::run(config);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.assignments.size(), 3U);
  EXPECT_FALSE(result.capacity_issue);
}

TEST(ScenarioWarehouseTest, StressCapacityIssue) {
  to::app::WorkflowConfig stress;
  stress.id = "stress";
  stress.actors.push_back(
      to::app::ActorConfig{.id = "r1", .type = "robot", .capacity = 1, .windows = {{.start = 0, .end = 100}}});
  for (int i = 0; i < 5; ++i)
    stress.tasks.push_back(to::app::TaskConfig{.id = "t" + std::to_string(i),
                                               .requested_time = 0,
                                               .duration = 50,
                                               .deadline = 50,
                                               .allowed_actor_types = {"robot"},
                                               .phase_durations = {}});
  const to::app::RunResult result = to::app::run(stress);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.capacity_issue);
  EXPECT_GE(result.unfulfilled_task_ids.size(), 1U);
  EXPECT_LT(result.assignments.size(), 5U);
}

TEST(ScenarioWarehouseTest, MixedActors) {
  to::app::WorkflowConfig mixed;
  mixed.id = "mixed";
  mixed.actors.push_back(
      to::app::ActorConfig{.id = "r1", .type = "robot", .capacity = 1, .windows = {{.start = 0, .end = 200}}});
  mixed.actors.push_back(
      to::app::ActorConfig{.id = "m1", .type = "machine", .capacity = 1, .windows = {{.start = 100, .end = 300}}});
  mixed.tasks.push_back(to::app::TaskConfig{.id = "j1",
                                            .requested_time = 0,
                                            .duration = 50,
                                            .deadline = 150,
                                            .allowed_actor_types = {"robot"},
                                            .phase_durations = {}});
  mixed.tasks.push_back(to::app::TaskConfig{.id = "j2",
                                            .requested_time = 100,
                                            .duration = 50,
                                            .deadline = 200,
                                            .allowed_actor_types = {"machine"},
                                            .phase_durations = {}});
  mixed.tasks.push_back(to::app::TaskConfig{.id = "j3",
                                            .requested_time = 50,
                                            .duration = 30,
                                            .deadline = 180,
                                            .allowed_actor_types = {"robot", "machine"},
                                            .phase_durations = {}});
  const to::app::RunResult result = to::app::run(mixed);
  EXPECT_TRUE(result.ok);
  EXPECT_GE(result.assignments.size(), 2U);
  EXPECT_LE(result.assignments.size(), 3U);
}
}  // namespace
