#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__MODEL_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__MODEL_HPP_
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/types.hpp"

namespace task_orchestrator::optimizer {

struct OptimizerActor {
  ActorId id;
  std::string type;
  int capacity = 1;
  std::vector<AvailabilityWindow> availability_windows;
  std::vector<std::string> capabilities;
  double execution_cost_per_unit = 0.0;
};

struct OptimizerTask {
  TaskId id;
  Duration duration = 0;
  Time release_time = 0;
  std::optional<Time> latest_start_time;
  std::optional<Time> deadline;
  Priority priority = 0;
  int demand = 1;
  bool mandatory = true;
  bool preemptible = false;
  std::vector<std::string> allowed_actor_types;
  std::vector<ActorId> allowed_actor_ids;
  std::vector<ActorId> preferred_actor_ids;
  std::vector<std::string> required_capabilities;
  std::vector<TaskId> dependency_task_ids;
  std::vector<TaskId> mutually_exclusive_task_ids;
  std::unordered_map<ActorId, Time> actor_distances;
  double tardiness_cost_per_unit = 0.0;
  double early_start_bonus = 0.0;
};

struct OptimizationModel {
  WorkflowId id;
  std::vector<OptimizerActor> actors;
  std::vector<OptimizerTask> tasks;
};

struct OptimizedAssignment {
  TaskId task_id;
  ActorId actor_id;
  Time start_time = 0;
  Time end_time = 0;
};

struct OptimizationStats {
  size_t search_nodes = 0;
  size_t pruned_nodes = 0;
};

struct OptimizationSolution {
  bool ok = true;
  std::vector<OptimizedAssignment> assignments;
  std::vector<TaskId> unfulfilled_task_ids;
  std::string backend_name;
  OptimizationStats stats;
  std::string error_message;
};

}  // namespace task_orchestrator::optimizer

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__MODEL_HPP_
