#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNTIME_SERVICE_SRC__IN_MEMORY_RUNTIME_SERVICE_DETAIL_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNTIME_SERVICE_SRC__IN_MEMORY_RUNTIME_SERVICE_DETAIL_HPP_

#include <algorithm>
#include <future>
#include <ranges>
#include <unordered_map>
#include <utility>

#include "config/config.hpp"
#include "protocol/runtime_api.hpp"
#include "runner/run_result.hpp"

namespace task_orchestrator::app::detail {

namespace pb = ::task_orchestrator::protocol::pb;

inline constexpr Time kRuntimeOverrideAvailabilityStart = 0;
inline constexpr Time kRuntimeOverrideAvailabilityEnd = 1000000;

template <typename Result>
std::future<Result> make_ready_future(Result value) {
  std::promise<Result> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

inline void remove_completed_task_dependencies(WorkflowConfig& config, const TaskId& completed_task_id) {
  for (TaskConfig& task : config.tasks) {
    std::erase(task.dependency_task_ids, completed_task_id);
  }
}

inline AvailabilityWindowConfig to_app_window(const pb::AvailabilityWindow& window) {
  return AvailabilityWindowConfig{
      .start = window.start(),
      .end = window.end(),
  };
}

inline ActorConfig to_app_actor(const pb::ActorConfig& actor) {
  ActorConfig app_actor{
      .id = actor.id(),
      .type = actor.type(),
  };
  if (actor.has_capacity()) {
    app_actor.capacity = actor.capacity();
  }
  app_actor.windows.reserve(static_cast<std::size_t>(actor.windows_size()));
  std::ranges::transform(actor.windows(),
                         std::back_inserter(app_actor.windows),
                         [](const pb::AvailabilityWindow& window) { return to_app_window(window); });
  app_actor.capabilities.assign(actor.capabilities().begin(), actor.capabilities().end());
  if (actor.has_execution_cost_per_unit()) {
    app_actor.execution_cost_per_unit = actor.execution_cost_per_unit();
  }
  return app_actor;
}

inline TaskConfig to_app_task(const pb::TaskConfig& task) {
  TaskConfig app_task{
      .id = task.id(),
      .requested_time = task.requested_time(),
      .duration = task.duration(),
      .latest_start_time = task.latest_start_time(),
      .deadline = task.deadline(),
      .priority = static_cast<Priority>(task.priority()),
  };
  if (task.has_demand()) {
    app_task.demand = task.demand();
  }
  if (task.has_mandatory()) {
    app_task.mandatory = task.mandatory();
  }
  if (task.has_preemptible()) {
    app_task.preemptible = task.preemptible();
  }
  app_task.allowed_actor_types.assign(task.allowed_actor_types().begin(), task.allowed_actor_types().end());
  app_task.allowed_actor_ids.assign(task.allowed_actor_ids().begin(), task.allowed_actor_ids().end());
  app_task.preferred_actor_ids.assign(task.preferred_actor_ids().begin(), task.preferred_actor_ids().end());
  app_task.required_capabilities.assign(task.required_capabilities().begin(), task.required_capabilities().end());
  app_task.dependency_task_ids.assign(task.dependency_task_ids().begin(), task.dependency_task_ids().end());
  app_task.mutually_exclusive_task_ids.assign(task.mutually_exclusive_task_ids().begin(),
                                              task.mutually_exclusive_task_ids().end());
  for (const pb::ActorDistance& actor_distance : task.actor_distances()) {
    app_task.actor_distances.emplace(actor_distance.actor_id(), actor_distance.distance());
  }
  app_task.tardiness_cost_per_unit = task.tardiness_cost_per_unit();
  app_task.early_start_bonus = task.early_start_bonus();
  app_task.phase_durations.assign(task.phase_durations().begin(), task.phase_durations().end());
  return app_task;
}

inline WorkflowConfig::OptimizationConfig::ObjectiveConfig to_app_objective(const pb::ObjectiveConfig& objective) {
  WorkflowConfig::OptimizationConfig::ObjectiveConfig app_objective;
  if (objective.has_fulfilled_task_weight()) {
    app_objective.fulfilled_task_weight = objective.fulfilled_task_weight();
  }
  if (objective.has_priority_weight()) {
    app_objective.priority_weight = objective.priority_weight();
  }
  if (objective.has_makespan_weight()) {
    app_objective.makespan_weight = objective.makespan_weight();
  }
  if (objective.has_travel_distance_weight()) {
    app_objective.travel_distance_weight = objective.travel_distance_weight();
  }
  if (objective.has_tardiness_weight()) {
    app_objective.tardiness_weight = objective.tardiness_weight();
  }
  if (objective.has_execution_cost_weight()) {
    app_objective.execution_cost_weight = objective.execution_cost_weight();
  }
  if (objective.has_preferred_actor_weight()) {
    app_objective.preferred_actor_weight = objective.preferred_actor_weight();
  }
  return app_objective;
}

inline WorkflowConfig to_app_workflow(const pb::WorkflowConfig& workflow) {
  WorkflowConfig app_workflow{
      .id = workflow.id(),
  };
  if (workflow.has_optimization()) {
    if (workflow.optimization().has_backend()) {
      app_workflow.optimization.backend = workflow.optimization().backend();
    }
    if (workflow.optimization().has_time_limit_ms()) {
      app_workflow.optimization.time_limit_ms = workflow.optimization().time_limit_ms();
    }
    if (workflow.optimization().has_relative_gap_limit()) {
      app_workflow.optimization.relative_gap_limit = workflow.optimization().relative_gap_limit();
    }
    if (workflow.optimization().has_num_search_workers()) {
      app_workflow.optimization.num_search_workers = std::max(1, workflow.optimization().num_search_workers());
    }
    if (workflow.optimization().has_allow_partial_plan()) {
      app_workflow.optimization.allow_partial_plan = workflow.optimization().allow_partial_plan();
    }
    if (workflow.optimization().has_objective()) {
      app_workflow.optimization.objective = to_app_objective(workflow.optimization().objective());
    }
  }
  app_workflow.actors.reserve(static_cast<std::size_t>(workflow.actors_size()));
  std::ranges::transform(workflow.actors(), std::back_inserter(app_workflow.actors), [](const pb::ActorConfig& actor) {
    return to_app_actor(actor);
  });
  app_workflow.tasks.reserve(static_cast<std::size_t>(workflow.tasks_size()));
  std::ranges::transform(workflow.tasks(), std::back_inserter(app_workflow.tasks), [](const pb::TaskConfig& task) {
    return to_app_task(task);
  });
  return app_workflow;
}

inline void populate_proto_run_result(const RunResult& result,
                                      const WorkflowConfig& workflow,
                                      pb::RunResult* proto_result) {
  proto_result->set_ok(result.ok);
  proto_result->set_capacity_issue(result.capacity_issue);
  proto_result->set_error_message(result.error_message);

  std::unordered_map<TaskId, Duration> task_durations;
  task_durations.reserve(workflow.tasks.size());
  for (const TaskConfig& task : workflow.tasks) {
    task_durations.emplace(task.id, task.duration);
  }

  for (const Assignment& assignment : result.assignments) {
    pb::Assignment* proto_assignment = proto_result->add_assignments();
    proto_assignment->set_task_id(assignment.task_id);
    proto_assignment->set_actor_id(assignment.actor_id);
    proto_assignment->set_start_time(assignment.start_time);
    const auto duration_it = task_durations.find(assignment.task_id);
    const Duration duration = duration_it != task_durations.end() ? duration_it->second : 0;
    proto_assignment->set_end_time(assignment.start_time + duration);
  }

  for (const TaskId& task_id : result.unfulfilled_task_ids) {
    proto_result->add_unfulfilled_task_ids(task_id);
  }
}

inline protocol::RuntimeApiResponse make_runtime_response(const RunResult& result, const WorkflowConfig& workflow) {
  protocol::RuntimeApiResponse response;
  response.set_ok(result.ok);
  response.set_error_message(result.error_message);
  populate_proto_run_result(result, workflow, response.mutable_result());
  return response;
}

inline protocol::RuntimeApiResponse make_runtime_error_response(std::string error_message) {
  protocol::RuntimeApiResponse response;
  response.set_ok(false);
  response.set_error_message(std::move(error_message));
  response.mutable_result()->set_ok(false);
  response.mutable_result()->set_error_message(response.error_message());
  return response;
}

inline protocol::WorkflowEvent make_event(pb::WorkflowEventType event_type,
                                          std::string workflow_id,
                                          std::string detail = {}) {
  protocol::WorkflowEvent event;
  event.set_type(event_type);
  event.set_workflow_id(std::move(workflow_id));
  event.set_detail(std::move(detail));
  return event;
}

inline protocol::WorkflowEvent make_response_event(pb::WorkflowEventType event_type,
                                                   const WorkflowId& workflow_id,
                                                   const protocol::RuntimeApiResponse& response,
                                                   std::string detail = {}) {
  protocol::WorkflowEvent event = make_event(event_type, workflow_id, std::move(detail));
  *event.mutable_response() = response;
  return event;
}

inline Duration task_duration_for_id(const WorkflowConfig& workflow, const TaskId& task_id) {
  const auto task_it = std::ranges::find(workflow.tasks, task_id, &TaskConfig::id);
  return task_it == workflow.tasks.end() ? 0 : task_it->duration;
}

inline protocol::RuntimeApiResponse consume_final_response(protocol::WorkflowEventStream event_stream) {
  protocol::RuntimeApiResponse response =
      make_runtime_error_response("Workflow execution did not produce a final response.");
  for (const protocol::WorkflowEvent& event : event_stream) {
    if (event.has_response()) {
      response = event.response();
    }
  }
  return response;
}

}  // namespace task_orchestrator::app::detail

#endif
