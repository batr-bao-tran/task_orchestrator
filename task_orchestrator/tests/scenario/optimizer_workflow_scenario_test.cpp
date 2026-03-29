#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "task_orchestrator/optimizer/optimizer.hpp"

namespace {
namespace too = task_orchestrator::optimizer;

struct OptimizerScenarioCase {
  std::string name;
  std::function<too::OptimizationModel()> build_model;
  too::OptimizationOptions options;
  std::vector<std::string> expected_assigned_task_ids;
  std::vector<std::string> expected_unfulfilled_task_ids;
};

class OptimizerWorkflowScenarioParamTest : public ::testing::TestWithParam<OptimizerScenarioCase> {};

std::vector<std::string> sorted_copy(std::vector<std::string> values) {
  std::ranges::sort(values);
  return values;
}

TEST_P(OptimizerWorkflowScenarioParamTest, ProducesDeterministicPlansAcrossWorkflowPatterns) {
  const OptimizerScenarioCase& test_case = GetParam();
  const too::WorkflowOptimizer optimizer(test_case.options);
  const too::OptimizationSolution solution = optimizer.optimize(test_case.build_model());

  ASSERT_TRUE(solution.ok) << solution.error_message;
  std::vector<std::string> assigned_task_ids;
  assigned_task_ids.reserve(solution.assignments.size());
  for (const too::OptimizedAssignment& assignment : solution.assignments) {
    assigned_task_ids.push_back(assignment.task_id);
  }
  std::ranges::sort(assigned_task_ids);

  EXPECT_EQ(sorted_copy(test_case.expected_assigned_task_ids), assigned_task_ids);
  EXPECT_EQ(sorted_copy(test_case.expected_unfulfilled_task_ids), sorted_copy(solution.unfulfilled_task_ids));
}

INSTANTIATE_TEST_SUITE_P(
    WorkflowScenarios,
    OptimizerWorkflowScenarioParamTest,
    ::testing::Values(
        OptimizerScenarioCase{
            .name = "capability_fragmentation_prefers_hybrid_actor_for_follow_up",
            .build_model =
                [] {
                  too::OptimizationModel model;
                  model.id = "capability_fragmentation_prefers_hybrid_actor_for_follow_up";
                  model.actors = {
                      {.id = "scan_only",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 0, .end = 6}},
                       .capabilities = {"scan"},
                       .execution_cost_per_unit = 1.0},
                      {.id = "hybrid",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 0, .end = 20}},
                       .capabilities = {"scan", "lift"},
                       .execution_cost_per_unit = 2.0},
                  };
                  model.tasks = {
                      {.id = "scan",
                       .duration = 3,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 10,
                       .priority = 10,
                       .demand = 1,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {"scan_only"},
                       .required_capabilities = {"scan"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                      {.id = "lift",
                       .duration = 4,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 18,
                       .priority = 9,
                       .demand = 1,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {"hybrid"},
                       .required_capabilities = {"lift"},
                       .dependency_task_ids = {"scan"},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                  };
                  return model;
                },
            .options =
                too::OptimizationOptions{
                    .backend_kind = too::BackendKind::IndexedExact,
                    .allow_partial_plan = false,
                    .objective = {},
                },
            .expected_assigned_task_ids = {"lift", "scan"},
            .expected_unfulfilled_task_ids = {},
        },
        OptimizerScenarioCase{
            .name = "partial_plan_drops_conflicting_low_priority_branch",
            .build_model =
                [] {
                  too::OptimizationModel model;
                  model.id = "partial_plan_drops_conflicting_low_priority_branch";
                  model.actors = {
                      {.id = "robot_1",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 0, .end = 20}},
                       .capabilities = {"pack"},
                       .execution_cost_per_unit = 1.0},
                  };
                  model.tasks = {
                      {.id = "critical_pack",
                       .duration = 4,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 12,
                       .priority = 10,
                       .demand = 1,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {},
                       .required_capabilities = {"pack"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {"optional_pack"},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                      {.id = "optional_pack",
                       .duration = 4,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 16,
                       .priority = 1,
                       .demand = 1,
                       .mandatory = false,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {},
                       .required_capabilities = {"pack"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {"critical_pack"},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                  };
                  return model;
                },
            .options =
                too::OptimizationOptions{
                    .backend_kind = too::BackendKind::IndexedExact,
                    .allow_partial_plan = true,
                    .objective = {},
                },
            .expected_assigned_task_ids = {"critical_pack"},
            .expected_unfulfilled_task_ids = {"optional_pack"},
        },
        OptimizerScenarioCase{
            .name = "demand_pressure_respects_large_actor_window",
            .build_model =
                [] {
                  too::OptimizationModel model;
                  model.id = "demand_pressure_respects_large_actor_window";
                  model.actors = {
                      {.id = "small",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 0, .end = 20}},
                       .capabilities = {"pack"},
                       .execution_cost_per_unit = 1.0},
                      {.id = "large",
                       .type = "robot",
                       .capacity = 2,
                       .availability_windows = {{.start = 2, .end = 20}},
                       .capabilities = {"pack"},
                       .execution_cost_per_unit = 2.0},
                  };
                  model.tasks = {
                      {.id = "bulk_pack",
                       .duration = 5,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 18,
                       .priority = 8,
                       .demand = 2,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {"large"},
                       .required_capabilities = {"pack"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                      {.id = "single_pack",
                       .duration = 3,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 12,
                       .priority = 6,
                       .demand = 1,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {"small"},
                       .required_capabilities = {"pack"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                  };
                  return model;
                },
            .options =
                too::OptimizationOptions{
                    .backend_kind = too::BackendKind::IndexedExact,
                    .allow_partial_plan = false,
                    .objective = {},
                },
            .expected_assigned_task_ids = {"bulk_pack", "single_pack"},
            .expected_unfulfilled_task_ids = {},
        },
        OptimizerScenarioCase{
            .name = "allowed_actor_override_beats_preference_when_only_one_actor_is_eligible",
            .build_model =
                [] {
                  too::OptimizationModel model;
                  model.id = "allowed_actor_override_beats_preference_when_only_one_actor_is_eligible";
                  model.actors = {
                      {.id = "robot_1",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 0, .end = 20}},
                       .capabilities = {"scan"},
                       .execution_cost_per_unit = 1.0},
                      {.id = "robot_2",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 0, .end = 20}},
                       .capabilities = {"scan"},
                       .execution_cost_per_unit = 1.0},
                  };
                  model.tasks = {
                      {.id = "inspect",
                       .duration = 3,
                       .release_time = 0,
                       .latest_start_time = std::nullopt,
                       .deadline = 10,
                       .priority = 8,
                       .demand = 1,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {"robot_2"},
                       .preferred_actor_ids = {"robot_1"},
                       .required_capabilities = {"scan"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {{"robot_1", 1}, {"robot_2", 9}},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                  };
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
        },
        OptimizerScenarioCase{
            .name = "latest_start_conflict_leaves_optional_work_unfulfilled_while_required_work_proceeds",
            .build_model =
                [] {
                  too::OptimizationModel model;
                  model.id = "latest_start_conflict_leaves_optional_work_unfulfilled_while_required_work_proceeds";
                  model.actors = {
                      {.id = "robot_1",
                       .type = "robot",
                       .capacity = 1,
                       .availability_windows = {{.start = 5, .end = 20}},
                       .capabilities = {"pack"},
                       .execution_cost_per_unit = 1.0},
                  };
                  model.tasks = {
                      {.id = "optional_pack",
                       .duration = 3,
                       .release_time = 0,
                       .latest_start_time = 1,
                       .deadline = 10,
                       .priority = 3,
                       .demand = 1,
                       .mandatory = false,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {},
                       .required_capabilities = {"pack"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                      {.id = "required_pack",
                       .duration = 4,
                       .release_time = 5,
                       .latest_start_time = 8,
                       .deadline = 14,
                       .priority = 9,
                       .demand = 1,
                       .mandatory = true,
                       .preemptible = false,
                       .allowed_actor_types = {"robot"},
                       .allowed_actor_ids = {},
                       .preferred_actor_ids = {},
                       .required_capabilities = {"pack"},
                       .dependency_task_ids = {},
                       .mutually_exclusive_task_ids = {},
                       .actor_distances = {},
                       .tardiness_cost_per_unit = 0.0,
                       .early_start_bonus = 0.0},
                  };
                  return model;
                },
            .options =
                too::OptimizationOptions{
                    .backend_kind = too::BackendKind::IndexedExact,
                    .allow_partial_plan = true,
                    .objective = {},
                },
            .expected_assigned_task_ids = {"required_pack"},
            .expected_unfulfilled_task_ids = {"optional_pack"},
        }),
    [](const ::testing::TestParamInfo<OptimizerScenarioCase>& info) { return info.param.name; });

}  // namespace
