#include "task_orchestrator/optimizer/constraint_index.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

namespace task_orchestrator::optimizer {
namespace {

std::vector<const OptimizerActor*> build_eligible_actors_for_task(
    const OptimizationModel& model,
    const std::unordered_map<ActorId, const OptimizerActor*>& actors_by_id,
    const std::unordered_map<std::string, std::vector<const OptimizerActor*>>& actors_by_type,
    const OptimizerTask& task) {
  std::vector<const OptimizerActor*> eligible_actors;
  if (!task.allowed_actor_ids.empty()) {
    eligible_actors.reserve(task.allowed_actor_ids.size());
    std::ranges::for_each(task.allowed_actor_ids, [&](const ActorId& allowed_actor_id) {
      const auto actor_it = actors_by_id.find(allowed_actor_id);
      if (actor_it == actors_by_id.end()) {
        return;
      }
      const OptimizerActor* actor = actor_it->second;
      if (!task.allowed_actor_types.empty() &&
          std::ranges::find(task.allowed_actor_types, actor->type) == task.allowed_actor_types.end()) {
        return;
      }
      eligible_actors.push_back(actor);
    });
    return eligible_actors;
  }

  if (!task.allowed_actor_types.empty()) {
    size_t reserved_actor_count = 0;
    std::ranges::for_each(task.allowed_actor_types, [&](const std::string& allowed_actor_type) {
      const auto type_it = actors_by_type.find(allowed_actor_type);
      if (type_it != actors_by_type.end()) {
        reserved_actor_count += type_it->second.size();
      }
    });
    eligible_actors.reserve(reserved_actor_count);
    std::ranges::for_each(task.allowed_actor_types, [&](const std::string& allowed_actor_type) {
      if (const auto type_it = actors_by_type.find(allowed_actor_type); type_it != actors_by_type.end()) {
        eligible_actors.insert(eligible_actors.end(), type_it->second.begin(), type_it->second.end());
      }
    });
    std::ranges::sort(eligible_actors, {}, &OptimizerActor::id);
    const auto duplicate_actor_range = std::ranges::unique(eligible_actors);
    eligible_actors.erase(duplicate_actor_range.begin(), duplicate_actor_range.end());
    return eligible_actors;
  }

  eligible_actors.reserve(model.actors.size());
  std::ranges::transform(
      model.actors, std::back_inserter(eligible_actors), [](const OptimizerActor& actor) { return &actor; });
  return eligible_actors;
}

}  // namespace

ConstraintIndex::ConstraintIndex(const OptimizationModel& model) : model_(model) {
  tasks_by_id_.reserve(model_.tasks.size());
  actors_by_id_.reserve(model_.actors.size());
  actors_by_type_.reserve(model_.actors.size());
  eligible_actors_by_task_.reserve(model_.tasks.size());
  successors_by_task_.reserve(model_.tasks.size());

  for (const OptimizerTask& task : model_.tasks) {
    tasks_by_id_[task.id] = &task;
    for (const TaskId& dependency_id : task.dependency_task_ids) {
      successors_by_task_[dependency_id].push_back(task.id);
    }
  }
  for (const OptimizerActor& actor : model_.actors) {
    actors_by_id_[actor.id] = &actor;
    actors_by_type_[actor.type].push_back(&actor);
  }

  for (const OptimizerTask& task : model_.tasks) {
    eligible_actors_by_task_.emplace(task.id,
                                     build_eligible_actors_for_task(model_, actors_by_id_, actors_by_type_, task));
  }
}

const OptimizerTask* ConstraintIndex::task(const TaskId& id) const {
  const auto it = tasks_by_id_.find(id);
  return it == tasks_by_id_.end() ? nullptr : it->second;
}

const OptimizerActor* ConstraintIndex::actor(const ActorId& id) const {
  const auto it = actors_by_id_.find(id);
  return it == actors_by_id_.end() ? nullptr : it->second;
}

const std::vector<const OptimizerActor*>& ConstraintIndex::eligible_actors_for_task(const OptimizerTask& task) const {
  static const std::vector<const OptimizerActor*> kNoEligibleActors;
  const auto eligible_actor_it = eligible_actors_by_task_.find(task.id);
  return eligible_actor_it == eligible_actors_by_task_.end() ? kNoEligibleActors : eligible_actor_it->second;
}

const std::vector<TaskId>& ConstraintIndex::successors(const TaskId& id) const {
  static const std::vector<TaskId> kEmpty;
  const auto it = successors_by_task_.find(id);
  return it == successors_by_task_.end() ? kEmpty : it->second;
}

bool ConstraintIndex::dependencies_satisfied(const OptimizerTask& task,
                                             const std::unordered_set<TaskId>& scheduled,
                                             const std::unordered_set<TaskId>& dropped) {
  return std::ranges::all_of(task.dependency_task_ids, [&scheduled, &dropped](const TaskId& dependency_id) {
    return scheduled.contains(dependency_id) && !dropped.contains(dependency_id);
  });
}

bool ConstraintIndex::dependency_blocked(const OptimizerTask& task, const std::unordered_set<TaskId>& dropped) {
  return std::ranges::any_of(task.dependency_task_ids,
                             [&dropped](const TaskId& dependency_id) { return dropped.contains(dependency_id); });
}

}  // namespace task_orchestrator::optimizer
