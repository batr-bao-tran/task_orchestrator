#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PROCESS_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PROCESS_HPP_
#include <optional>
#include <vector>

#include "task_orchestrator/data/types.hpp"

namespace task_orchestrator {

struct Process {
  ProcessId id;
  PhaseId phase_id;
  std::vector<SubProcessId> sub_process_ids;
  Duration estimated_duration = 0;
  Priority priority = 0;
  std::optional<Time> deadline;

  /** All schedulable task IDs: this process and its sub-processes. */
  std::vector<TaskId> task_ids() const;
};

}  // namespace task_orchestrator

#endif
