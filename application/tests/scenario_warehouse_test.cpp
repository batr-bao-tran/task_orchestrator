#include <gtest/gtest.h>

#include "config/config.hpp"
#include "runner/runner.hpp"

using namespace task_orchestrator::app;

TEST(ScenarioWarehouseTest, SimpleThreeTasks) {
  std::string yaml = R"(
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
  WorkflowConfig cfg = load_config_from_string(yaml);
  RunResult r = run(cfg);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.assignments.size(), 3u);
  EXPECT_FALSE(r.capacity_issue);
}

TEST(ScenarioWarehouseTest, StressCapacityIssue) {
  WorkflowConfig stress;
  stress.id = "stress";
  stress.actors.push_back({"r1", "robot", 1, {{0, 100}}});
  for (int i = 0; i < 5; ++i) stress.tasks.push_back({"t" + std::to_string(i), 0, 50, 50, {"robot"}});
  RunResult r2 = run(stress);
  EXPECT_TRUE(r2.ok);
  EXPECT_TRUE(r2.capacity_issue);
  EXPECT_GE(r2.unfulfilled_task_ids.size(), 1u);
  EXPECT_LT(r2.assignments.size(), 5u);
}

TEST(ScenarioWarehouseTest, MixedActors) {
  WorkflowConfig mixed;
  mixed.id = "mixed";
  mixed.actors.push_back({"r1", "robot", 1, {{0, 200}}});
  mixed.actors.push_back({"m1", "machine", 1, {{100, 300}}});
  mixed.tasks.push_back({"j1", 0, 50, 150, {"robot"}});
  mixed.tasks.push_back({"j2", 100, 50, 200, {"machine"}});
  mixed.tasks.push_back({"j3", 50, 30, 180, {"robot", "machine"}});
  RunResult r3 = run(mixed);
  EXPECT_TRUE(r3.ok);
  EXPECT_GE(r3.assignments.size(), 2u);
  EXPECT_LE(r3.assignments.size(), 3u);
}
