#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__EDF_STRATEGY_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__EDF_STRATEGY_HPP_
#include <algorithm>

#include "task_orchestrator/strategy/scheduling_strategy.hpp"

namespace task_orchestrator {

/** Earliest deadline first: higher priority first, then earlier deadline. */
class EDFStrategy : public SchedulingStrategy {
 public:
  void order_tasks(std::vector<TaskInfo>& tasks) const override {
    std::ranges::sort(tasks, [](const TaskInfo& a, const TaskInfo& b) {
      if (a.priority != b.priority) {
        return a.priority > b.priority;
      }
      if (a.deadline.has_value() != b.deadline.has_value()) {
        return a.deadline.has_value();
      }
      if (a.deadline && b.deadline) {
        return *a.deadline < *b.deadline;
      }
      return false;
    });
  }
};

}  // namespace task_orchestrator

#endif
