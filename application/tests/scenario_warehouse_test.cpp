#include <gtest/gtest.h>

#include "config/config.hpp"
#include "runner/runner.hpp"

namespace {
namespace to = task_orchestrator;

to::app::TaskConfig make_task(std::string id,
                              to::Time requested_time,
                              to::Duration duration,
                              to::Time deadline,
                              to::Priority priority,
                              std::vector<std::string> allowed_actor_types) {
  return to::app::TaskConfig{
      .id = std::move(id),
      .requested_time = requested_time,
      .duration = duration,
      .latest_start_time = 0,
      .deadline = deadline,
      .priority = priority,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = std::move(allowed_actor_types),
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
      .phase_durations = {},
  };
}

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
  stress.actors.push_back(to::app::ActorConfig{.id = "r1",
                                               .type = "robot",
                                               .capacity = 1,
                                               .windows = {{.start = 0, .end = 100}},
                                               .capabilities = {},
                                               .execution_cost_per_unit = 0.0});
  for (int i = 0; i < 5; ++i) stress.tasks.push_back(make_task("t" + std::to_string(i), 0, 50, 50, 0, {"robot"}));
  const to::app::RunResult result = to::app::run(stress);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.capacity_issue);
  EXPECT_GE(result.unfulfilled_task_ids.size(), 1U);
  EXPECT_LT(result.assignments.size(), 5U);
}

TEST(ScenarioWarehouseTest, MixedActors) {
  to::app::WorkflowConfig mixed;
  mixed.id = "mixed";
  mixed.actors.push_back(to::app::ActorConfig{.id = "r1",
                                              .type = "robot",
                                              .capacity = 1,
                                              .windows = {{.start = 0, .end = 200}},
                                              .capabilities = {},
                                              .execution_cost_per_unit = 0.0});
  mixed.actors.push_back(to::app::ActorConfig{.id = "m1",
                                              .type = "machine",
                                              .capacity = 1,
                                              .windows = {{.start = 100, .end = 300}},
                                              .capabilities = {},
                                              .execution_cost_per_unit = 0.0});
  mixed.tasks.push_back(make_task("j1", 0, 50, 150, 0, {"robot"}));
  mixed.tasks.push_back(make_task("j2", 100, 50, 200, 0, {"machine"}));
  mixed.tasks.push_back(make_task("j3", 50, 30, 180, 0, {"robot", "machine"}));
  const to::app::RunResult result = to::app::run(mixed);
  EXPECT_TRUE(result.ok);
  EXPECT_GE(result.assignments.size(), 2U);
  EXPECT_LE(result.assignments.size(), 3U);
}
}  // namespace
