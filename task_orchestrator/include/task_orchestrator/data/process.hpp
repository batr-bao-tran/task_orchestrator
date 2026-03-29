#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PROCESS_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PROCESS_HPP_
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/types.hpp"

namespace task_orchestrator {

struct Process {
  ProcessId id;
  PhaseId phase_id;
  std::vector<SubProcessId> sub_process_ids;
  Duration estimated_duration = 0;
  Priority priority = 0;
  std::optional<Time> deadline = std::nullopt;
  Time release_time = 0;
  std::optional<Time> latest_start_time = std::nullopt;
  int demand = 1;
  bool mandatory = true;
  bool preemptible = false;
  std::vector<std::string> allowed_actor_types{};
  std::vector<ActorId> allowed_actor_ids{};
  std::vector<ActorId> preferred_actor_ids{};
  std::vector<std::string> required_capabilities{};
  std::vector<TaskId> dependency_task_ids{};
  std::vector<TaskId> mutually_exclusive_task_ids{};
  std::unordered_map<ActorId, Time> actor_distances{};
  double tardiness_cost_per_unit = 0.0;
  double early_start_bonus = 0.0;

  /** All schedulable task IDs: this process and its sub-processes. */
  std::vector<TaskId> task_ids() const;
};

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PROCESS_HPP_
