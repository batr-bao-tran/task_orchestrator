#include "runner/runner.hpp"

#include <gtest/gtest.h>

#include "config/config.hpp"

namespace {
namespace to = task_orchestrator;

to::app::ActorConfig make_actor(std::string id, std::string type, int capacity, to::Time start, to::Time end) {
  return to::app::ActorConfig{
      .id = std::move(id),
      .type = std::move(type),
      .capacity = capacity,
      .windows = {{.start = start, .end = end}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  };
}

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

TEST(RunnerTest, TwoTasksTwoAssignments) {
  to::app::WorkflowConfig cfg;
  cfg.id = "test";
  cfg.actors.push_back(make_actor("r1", "robot", 2, 0, 1000));
  cfg.tasks.push_back(make_task("t1", 0, 10, 50, 0, {"robot"}));
  cfg.tasks.push_back(make_task("t2", 0, 10, 50, 0, {"robot"}));

  const to::app::RunResult result = to::app::run(cfg);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.assignments.size(), 2U);
  EXPECT_FALSE(result.capacity_issue);
  EXPECT_TRUE(result.unfulfilled_task_ids.empty());
}

TEST(RunnerTest, HonorsActorEligibilityAndPreferences) {
  to::app::WorkflowConfig cfg;
  cfg.id = "eligibility";
  cfg.actors.push_back(make_actor("robot_1", "robot", 1, 0, 100));
  cfg.actors.push_back(make_actor("robot_2", "robot", 1, 0, 100));
  auto task = make_task("t1", 0, 10, 30, 10, {"robot"});
  task.allowed_actor_ids = {"robot_1", "robot_2"};
  task.preferred_actor_ids = {"robot_2"};
  task.actor_distances = {{"robot_1", 20}, {"robot_2", 5}};
  cfg.tasks.push_back(std::move(task));

  const to::app::RunResult result = to::app::optimize(cfg);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("robot_2", result.assignments[0].actor_id);
}

TEST(RunnerTest, SchedulesDependentTasksAcrossPlanningHorizon) {
  to::app::WorkflowConfig cfg;
  cfg.id = "dependency";
  cfg.actors.push_back(make_actor("r1", "robot", 1, 0, 100));
  cfg.tasks.push_back(make_task("pick", 0, 10, 40, 10, {"robot"}));
  auto pack = make_task("pack", 0, 5, 50, 5, {"robot"});
  pack.dependency_task_ids = {"pick"};
  cfg.tasks.push_back(std::move(pack));

  const to::app::RunResult result = to::app::optimize(cfg);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ("pick", result.assignments[0].task_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
  EXPECT_EQ("pack", result.assignments[1].task_id);
  EXPECT_EQ(10, result.assignments[1].start_time);
}

TEST(RunnerTest, OptimizesNaturalLanguageRequest) {
  const std::string request = R"(
workflow inbound_ops
actors:
- actor robot_a type robot capacity 1 windows 0-100
- actor machine_a type machine capacity 1 windows 0-100
tasks:
- task inspect duration 5 release 0 deadline 20 priority 10 requires_type robot preferred_actor robot_a
- task seal duration 10 release 0 deadline 40 priority 5 requires_type machine depends_on inspect
)";

  const to::app::RunResult result = to::app::optimize_text(request);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ("inspect", result.assignments[0].task_id);
  EXPECT_EQ("robot_a", result.assignments[0].actor_id);
  EXPECT_EQ("seal", result.assignments[1].task_id);
  EXPECT_EQ(5, result.assignments[1].start_time);
}

TEST(RunnerTest, RequestedOptionalBackendFailsClearlyWhenNotLinked) {
  to::app::WorkflowConfig cfg;
  cfg.id = "backend_choice";
  cfg.optimization.backend = "ortools_cp_sat";
  cfg.actors.push_back(make_actor("r1", "robot", 1, 0, 100));
  cfg.tasks.push_back(make_task("t1", 0, 5, 20, 1, {"robot"}));

  const to::app::RunResult result = to::app::optimize(cfg);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(std::string::npos, result.error_message.find("not linked"));
}
}  // namespace
