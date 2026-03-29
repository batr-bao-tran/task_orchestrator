#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "task_orchestrator/optimizer/optimizer.hpp"

namespace {
namespace too = task_orchestrator::optimizer;

too::OptimizerActor make_actor(std::string id, int capacity, std::vector<std::string> capabilities = {}) {
  return too::OptimizerActor{
      .id = std::move(id),
      .type = "robot",
      .capacity = capacity,
      .availability_windows = {{.start = 0, .end = 40}},
      .capabilities = std::move(capabilities),
      .execution_cost_per_unit = 1.0,
  };
}

too::OptimizerTask make_task(std::string id,
                             task_orchestrator::Duration duration,
                             task_orchestrator::Priority priority) {
  return too::OptimizerTask{
      .id = std::move(id),
      .duration = duration,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = 30,
      .priority = priority,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  };
}

struct IndexedExactCase {
  std::string name;
  std::function<too::OptimizationModel()> build_model;
  too::OptimizationOptions options;
  std::vector<std::string> expected_assigned_task_ids;
  std::vector<std::string> expected_unfulfilled_task_ids;
  std::vector<std::string> expected_actor_ids;
};

class IndexedExactOptimizerParamTest : public ::testing::TestWithParam<IndexedExactCase> {};

std::vector<std::string> sorted_copy(std::vector<std::string> values) {
  std::ranges::sort(values);
  return values;
}

TEST_P(IndexedExactOptimizerParamTest, HonorsCoreConstraintSemantics) {
  const IndexedExactCase& test_case = GetParam();
  const too::WorkflowOptimizer optimizer(test_case.options);
  const too::OptimizationSolution solution = optimizer.optimize(test_case.build_model());

  ASSERT_TRUE(solution.ok) << solution.error_message;
  EXPECT_EQ("indexed_branch_and_bound", solution.backend_name);

  std::vector<std::string> assigned_task_ids;
  std::vector<std::string> actor_ids;
  assigned_task_ids.reserve(solution.assignments.size());
  actor_ids.reserve(solution.assignments.size());
  for (const too::OptimizedAssignment& assignment : solution.assignments) {
    assigned_task_ids.push_back(assignment.task_id);
    actor_ids.push_back(assignment.actor_id);
  }
  std::ranges::sort(assigned_task_ids);
  std::ranges::sort(actor_ids);

  EXPECT_EQ(sorted_copy(test_case.expected_assigned_task_ids), assigned_task_ids);
  EXPECT_EQ(sorted_copy(test_case.expected_unfulfilled_task_ids), sorted_copy(solution.unfulfilled_task_ids));
  EXPECT_EQ(sorted_copy(test_case.expected_actor_ids), actor_ids);
}

INSTANTIATE_TEST_SUITE_P(IndexedExactScenarios,
                         IndexedExactOptimizerParamTest,
                         ::testing::Values(
                             IndexedExactCase{
                                 .name = "demand_respects_actor_capacity",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "demand_respects_actor_capacity";
                                       model.actors = {
                                           make_actor("small", 1),
                                           make_actor("large", 2),
                                       };
                                       auto heavy = make_task("heavy", 4, 10);
                                       heavy.demand = 2;
                                       auto light = make_task("light", 2, 5);
                                       model.tasks = {heavy, light};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = false,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {"heavy", "light"},
                                 .expected_unfulfilled_task_ids = {},
                                 .expected_actor_ids = {"large", "small"},
                             },
                             IndexedExactCase{
                                 .name = "mutual_exclusion_keeps_high_priority_branch",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "mutual_exclusion_keeps_high_priority_branch";
                                       model.actors = {make_actor("robot_1", 1)};
                                       auto critical = make_task("critical", 3, 20);
                                       critical.mutually_exclusive_task_ids = {"fallback"};
                                       auto fallback = make_task("fallback", 3, 5);
                                       fallback.mutually_exclusive_task_ids = {"critical"};
                                       model.tasks = {critical, fallback};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = true,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {"critical"},
                                 .expected_unfulfilled_task_ids = {"fallback"},
                                 .expected_actor_ids = {"robot_1"},
                             },
                             IndexedExactCase{
                                 .name = "dependency_cascade_marks_successor_unfulfilled",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "dependency_cascade_marks_successor_unfulfilled";
                                       model.actors = {make_actor("robot_1", 1, {"scan"})};
                                       auto impossible = make_task("impossible", 5, 10);
                                       impossible.required_capabilities = {"lift"};
                                       impossible.mandatory = false;
                                       auto dependent = make_task("dependent", 2, 8);
                                       dependent.dependency_task_ids = {"impossible"};
                                       model.tasks = {impossible, dependent};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = true,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {},
                                 .expected_unfulfilled_task_ids = {"dependent", "impossible"},
                                 .expected_actor_ids = {},
                             },
                             IndexedExactCase{
                                 .name = "allowed_actor_ids_restrict_candidate_set",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "allowed_actor_ids_restrict_candidate_set";
                                       model.actors = {
                                           make_actor("robot_1", 1, {"scan"}),
                                           make_actor("robot_2", 1, {"scan"}),
                                       };
                                       auto inspect = make_task("inspect", 3, 7);
                                       inspect.allowed_actor_ids = {"robot_2"};
                                       inspect.preferred_actor_ids = {"robot_1"};
                                       inspect.required_capabilities = {"scan"};
                                       model.tasks = {inspect};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = false,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {"inspect"},
                                 .expected_unfulfilled_task_ids = {},
                                 .expected_actor_ids = {"robot_2"},
                             },
                             IndexedExactCase{
                                 .name = "latest_start_window_conflict_drops_optional_task",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "latest_start_window_conflict_drops_optional_task";
                                       model.actors = {
                                           too::OptimizerActor{
                                               .id = "robot_1",
                                               .type = "robot",
                                               .capacity = 1,
                                               .availability_windows = {{.start = 6, .end = 20}},
                                               .capabilities = {"lift"},
                                               .execution_cost_per_unit = 1.0,
                                           },
                                       };
                                       auto optional_pick = make_task("optional_pick", 3, 3);
                                       optional_pick.latest_start_time = 2;
                                       optional_pick.mandatory = false;
                                       optional_pick.required_capabilities = {"lift"};
                                       auto lift = make_task("lift", 4, 9);
                                       lift.required_capabilities = {"lift"};
                                       model.tasks = {optional_pick, lift};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = true,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {"lift"},
                                 .expected_unfulfilled_task_ids = {"optional_pick"},
                                 .expected_actor_ids = {"robot_1"},
                             },
                             IndexedExactCase{
                                 .name = "zero_duration_task_starts_at_release_time",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "zero_duration_task_starts_at_release_time";
                                       model.actors = {make_actor("robot_1", 1)};
                                       auto instant_task = make_task("instant_task", 0, 4);
                                       instant_task.release_time = 6;
                                       model.tasks = {instant_task};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = false,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {"instant_task"},
                                 .expected_unfulfilled_task_ids = {},
                                 .expected_actor_ids = {"robot_1"},
                             },
                             IndexedExactCase{
                                 .name = "short_window_is_skipped_before_feasible_window",
                                 .build_model =
                                     [] {
                                       too::OptimizationModel model;
                                       model.id = "short_window_is_skipped_before_feasible_window";
                                       model.actors = {
                                           too::OptimizerActor{
                                               .id = "robot_1",
                                               .type = "robot",
                                               .capacity = 1,
                                               .availability_windows = {{.start = 0, .end = 2},
                                                                        {.start = 5, .end = 12}},
                                               .capabilities = {},
                                               .execution_cost_per_unit = 1.0,
                                           },
                                       };
                                       model.tasks = {make_task("windowed_task", 3, 5)};
                                       return model;
                                     },
                                 .options =
                                     too::OptimizationOptions{
                                         .backend_kind = too::BackendKind::IndexedExact,
                                         .allow_partial_plan = false,
                                         .objective = {},
                                     },
                                 .expected_assigned_task_ids = {"windowed_task"},
                                 .expected_unfulfilled_task_ids = {},
                                 .expected_actor_ids = {"robot_1"},
                             }),
                         [](const ::testing::TestParamInfo<IndexedExactCase>& info) { return info.param.name; });

TEST(IndexedExactOptimizerTest, MandatoryUnschedulableModelFailsWithoutPartialPlan) {
  too::OptimizationModel model;
  model.id = "mandatory_unschedulable_model_fails_without_partial_plan";
  model.actors = {make_actor("robot_1", 1, {"scan"})};
  auto impossible = make_task("impossible", 5, 10);
  impossible.required_capabilities = {"lift"};
  model.tasks = {impossible};

  const too::WorkflowOptimizer optimizer(too::OptimizationOptions{
      .backend_kind = too::BackendKind::IndexedExact, .allow_partial_plan = false, .objective = {}});
  const too::OptimizationSolution solution = optimizer.optimize(model);

  EXPECT_FALSE(solution.ok);
  EXPECT_NE(std::string::npos, solution.error_message.find("No optimization result"));
}

TEST(IndexedExactOptimizerTest, ZeroDurationTaskUsesReleaseTimeAsStart) {
  too::OptimizationModel model;
  model.id = "zero_duration_release_time";
  model.actors = {make_actor("robot_1", 1)};
  auto instant_task = make_task("instant_task", 0, 4);
  instant_task.release_time = 6;
  model.tasks = {instant_task};

  const too::WorkflowOptimizer optimizer(too::OptimizationOptions{
      .backend_kind = too::BackendKind::IndexedExact, .allow_partial_plan = false, .objective = {}});
  const too::OptimizationSolution solution = optimizer.optimize(model);

  ASSERT_TRUE(solution.ok) << solution.error_message;
  ASSERT_EQ(1U, solution.assignments.size());
  EXPECT_EQ("instant_task", solution.assignments.front().task_id);
  EXPECT_EQ(6, solution.assignments.front().start_time);
  EXPECT_EQ(6, solution.assignments.front().end_time);
}

TEST(IndexedExactOptimizerTest, ShortAvailabilityWindowIsSkippedBeforeFeasibleWindow) {
  too::OptimizationModel model;
  model.id = "short_window_then_feasible_window";
  model.actors = {
      too::OptimizerActor{
          .id = "robot_1",
          .type = "robot",
          .capacity = 1,
          .availability_windows = {{.start = 0, .end = 2}, {.start = 5, .end = 12}},
          .capabilities = {},
          .execution_cost_per_unit = 1.0,
      },
  };
  model.tasks = {make_task("windowed_task", 3, 5)};

  const too::WorkflowOptimizer optimizer(too::OptimizationOptions{
      .backend_kind = too::BackendKind::IndexedExact, .allow_partial_plan = false, .objective = {}});
  const too::OptimizationSolution solution = optimizer.optimize(model);

  ASSERT_TRUE(solution.ok) << solution.error_message;
  ASSERT_EQ(1U, solution.assignments.size());
  EXPECT_EQ("windowed_task", solution.assignments.front().task_id);
  EXPECT_EQ(5, solution.assignments.front().start_time);
}

}  // namespace
