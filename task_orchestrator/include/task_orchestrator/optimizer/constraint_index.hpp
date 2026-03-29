#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__CONSTRAINT_INDEX_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__CONSTRAINT_INDEX_HPP_
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "task_orchestrator/optimizer/model.hpp"

namespace task_orchestrator::optimizer {

class ConstraintIndex {
 public:
  explicit ConstraintIndex(const OptimizationModel& model);

  const OptimizerTask* task(const TaskId& id) const;
  const OptimizerActor* actor(const ActorId& id) const;
  const std::vector<const OptimizerActor*>& eligible_actors_for_task(const OptimizerTask& task) const;
  const std::vector<TaskId>& successors(const TaskId& id) const;

  static bool dependencies_satisfied(const OptimizerTask& task,
                                     const std::unordered_set<TaskId>& scheduled,
                                     const std::unordered_set<TaskId>& dropped);
  static bool dependency_blocked(const OptimizerTask& task, const std::unordered_set<TaskId>& dropped);

 private:
  const OptimizationModel& model_;
  std::unordered_map<TaskId, const OptimizerTask*> tasks_by_id_;
  std::unordered_map<ActorId, const OptimizerActor*> actors_by_id_;
  std::unordered_map<std::string, std::vector<const OptimizerActor*>> actors_by_type_;
  std::unordered_map<TaskId, std::vector<const OptimizerActor*>> eligible_actors_by_task_;
  std::unordered_map<TaskId, std::vector<TaskId>> successors_by_task_;
};

}  // namespace task_orchestrator::optimizer

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__CONSTRAINT_INDEX_HPP_
