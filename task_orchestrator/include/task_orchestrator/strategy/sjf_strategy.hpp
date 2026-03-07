#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__SJF_STRATEGY_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__SJF_STRATEGY_HPP_
#include <algorithm>

#include "task_orchestrator/strategy/scheduling_strategy.hpp"

namespace task_orchestrator {

/** Shortest job first: prefer shorter estimated duration, then by priority and id. */
class SJFStrategy : public SchedulingStrategy {
 public:
  void order_tasks(std::vector<TaskInfo>& tasks) const override {
    std::sort(tasks.begin(), tasks.end(), [](const TaskInfo& a, const TaskInfo& b) {
      if (a.duration != b.duration) {
        return a.duration < b.duration;
      }
      if (a.priority != b.priority) {
        return a.priority > b.priority;
      }
      return a.id < b.id;
    });
  }
};

}  // namespace task_orchestrator

#endif
