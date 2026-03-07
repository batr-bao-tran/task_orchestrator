#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__PRIORITY_ONLY_STRATEGY_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__PRIORITY_ONLY_STRATEGY_HPP_
#include <algorithm>

#include "task_orchestrator/strategy/scheduling_strategy.hpp"

namespace task_orchestrator {

/** Priority only: higher priority first; ties broken by task id. */
class PriorityOnlyStrategy : public SchedulingStrategy {
 public:
  void order_tasks(std::vector<TaskInfo>& tasks) const override {
    std::ranges::sort(tasks, [](const TaskInfo& a, const TaskInfo& b) {
      if (a.priority != b.priority) {
        return a.priority > b.priority;
      }
      return a.id < b.id;
    });
  }
};

}  // namespace task_orchestrator

#endif
