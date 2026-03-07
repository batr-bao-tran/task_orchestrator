#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__SCHEDULING_STRATEGY_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__SCHEDULING_STRATEGY_HPP_
#include <optional>
#include <vector>

#include "task_orchestrator/data/types.hpp"

namespace task_orchestrator {

/** Per-task info used by the scheduler and by scheduling strategies to order work. */
struct TaskInfo {
  TaskId id;
  PhaseId phase_id;
  Duration duration;
  Priority priority;
  std::optional<Time> deadline;
};

/** Interface for pluggable scheduling strategies: order pending tasks before assignment. */
class SchedulingStrategy {
 public:
  virtual ~SchedulingStrategy() = default;
  /** Sort \p tasks in the order the scheduler should consider them (first = highest preference). */
  virtual void order_tasks(std::vector<TaskInfo>& tasks) const = 0;
};

}  // namespace task_orchestrator

#endif
