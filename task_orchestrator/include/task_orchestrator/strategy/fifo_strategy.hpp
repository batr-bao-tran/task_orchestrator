#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__FIFO_STRATEGY_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__FIFO_STRATEGY_HPP_
#include <algorithm>

#include "task_orchestrator/strategy/scheduling_strategy.hpp"

namespace task_orchestrator {

/** First-in-first-out: order by phase then task id (workflow submission order). */
class FIFOStrategy : public SchedulingStrategy {
 public:
  void order_tasks(std::vector<TaskInfo>& tasks) const override {
    std::ranges::sort(tasks, [](const TaskInfo& a, const TaskInfo& b) {
      if (a.phase_id != b.phase_id) {
        return a.phase_id < b.phase_id;
      }
      return a.id < b.id;
    });
  }
};

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_STRATEGY__FIFO_STRATEGY_HPP_
