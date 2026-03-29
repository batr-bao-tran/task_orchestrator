#include "runner/runner.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <ranges>
#include <tuple>
#include <utility>

#include "task_orchestrator/optimizer/model.hpp"
#include "task_orchestrator/optimizer/optimizer.hpp"

namespace task_orchestrator::app {
namespace {

std::optional<Time> optional_time_from_config_value(const Time configured_time) {
  return configured_time > 0 ? std::optional<Time>(configured_time) : std::nullopt;
}

optimizer::OptimizationOptions to_options(const WorkflowConfig::OptimizationConfig& config) {
  optimizer::OptimizationOptions options;
  options.backend_kind = optimizer::backend_kind_from_string(config.backend);
  options.time_limit_ms = config.time_limit_ms;
  options.relative_gap_limit = config.relative_gap_limit;
  options.num_search_workers = config.num_search_workers;
  options.allow_partial_plan = config.allow_partial_plan;
  options.objective.fulfilled_task_weight = config.objective.fulfilled_task_weight;
  options.objective.priority_weight = config.objective.priority_weight;
  options.objective.makespan_weight = config.objective.makespan_weight;
  options.objective.travel_distance_weight = config.objective.travel_distance_weight;
  options.objective.tardiness_weight = config.objective.tardiness_weight;
  options.objective.execution_cost_weight = config.objective.execution_cost_weight;
  options.objective.preferred_actor_weight = config.objective.preferred_actor_weight;
  return options;
}

optimizer::OptimizerActor to_optimizer_actor(const ActorConfig& actor_config) {
  optimizer::OptimizerActor actor{
      .id = actor_config.id,
      .type = actor_config.type,
      .capacity = actor_config.capacity,
      .availability_windows = {},
      .capabilities = actor_config.capabilities,
      .execution_cost_per_unit = actor_config.execution_cost_per_unit,
  };
  actor.availability_windows.reserve(actor_config.windows.size());
  std::ranges::transform(actor_config.windows,
                         std::back_inserter(actor.availability_windows),
                         [](const AvailabilityWindowConfig& window_config) {
                           return AvailabilityWindow{.start = window_config.start, .end = window_config.end};
                         });
  return actor;
}

optimizer::OptimizerTask to_optimizer_task(const TaskConfig& task_config) {
  return optimizer::OptimizerTask{
      .id = task_config.id,
      .duration = task_config.duration,
      .release_time = task_config.requested_time,
      .latest_start_time = optional_time_from_config_value(task_config.latest_start_time),
      .deadline = optional_time_from_config_value(task_config.deadline),
      .priority = task_config.priority,
      .demand = task_config.demand,
      .mandatory = task_config.mandatory,
      .preemptible = task_config.preemptible,
      .allowed_actor_types = task_config.allowed_actor_types,
      .allowed_actor_ids = task_config.allowed_actor_ids,
      .preferred_actor_ids = task_config.preferred_actor_ids,
      .required_capabilities = task_config.required_capabilities,
      .dependency_task_ids = task_config.dependency_task_ids,
      .mutually_exclusive_task_ids = task_config.mutually_exclusive_task_ids,
      .actor_distances = task_config.actor_distances,
      .tardiness_cost_per_unit = task_config.tardiness_cost_per_unit,
      .early_start_bonus = task_config.early_start_bonus,
  };
}

optimizer::OptimizationModel to_model(const WorkflowConfig& config) {
  optimizer::OptimizationModel model;
  model.id = config.id;
  model.actors.reserve(config.actors.size());
  model.tasks.reserve(config.tasks.size());
  std::ranges::transform(config.actors, std::back_inserter(model.actors), to_optimizer_actor);
  std::ranges::transform(config.tasks, std::back_inserter(model.tasks), to_optimizer_task);

  return model;
}

Assignment to_assignment(const optimizer::OptimizedAssignment& optimized_assignment) {
  return Assignment{
      .task_id = optimized_assignment.task_id,
      .actor_id = optimized_assignment.actor_id,
      .start_time = optimized_assignment.start_time,
  };
}

RunResult to_run_result(const optimizer::OptimizationSolution& solution) {
  RunResult result;
  result.ok = solution.ok;
  result.capacity_issue = !solution.unfulfilled_task_ids.empty();
  result.unfulfilled_task_ids = solution.unfulfilled_task_ids;
  result.error_message = solution.error_message;
  result.assignments.reserve(solution.assignments.size());
  std::ranges::transform(solution.assignments, std::back_inserter(result.assignments), to_assignment);

  std::ranges::sort(result.assignments, [](const Assignment& lhs, const Assignment& rhs) {
    return std::tuple(lhs.start_time, lhs.task_id, lhs.actor_id) <
           std::tuple(rhs.start_time, rhs.task_id, rhs.actor_id);
  });
  return result;
}

}  // namespace

RunResult optimize(const OptimizationRequest& config) {
  const optimizer::WorkflowOptimizer workflow_optimizer(to_options(config.optimization));
  return to_run_result(workflow_optimizer.optimize(to_model(config)));
}

RunResult run(const WorkflowConfig& config) { return optimize(config); }

RunResult optimize_text(std::string_view request) {
  const optimizer::WorkflowOptimizer workflow_optimizer;
  return to_run_result(workflow_optimizer.optimize_text(request));
}

}  // namespace task_orchestrator::app
