#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__SCHEDULER_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__SCHEDULER_HPP_
#include <ranges>
#include <unordered_map>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/types.hpp"
#include "task_orchestrator/strategy/scheduling_strategy.hpp"
#include "task_orchestrator/utils/generator.hpp"

namespace task_orchestrator {

struct Assignment {
  TaskId task_id;
  ActorId actor_id;
  Time start_time;
};

struct WorkflowState {
  std::vector<PhaseId> completed_phases;
  /** Tasks already assigned (e.g. in-flight). */
  std::vector<TaskId> assigned_tasks;
  /** Per-actor current load (optional override; else use Actor::current_load). */
  std::unordered_map<ActorId, int> actor_load;
};

struct ScheduleResult {
  bool ok = false;
  std::vector<Assignment> assignments;
  std::string error_message;
};

class ActorRegistry {
 public:
  void add(Actor a);
  const Actor* get(const ActorId& id) const;
  std::vector<ActorId> actor_ids() const;
  /** Mutable actor for load updates during simulation. */
  Actor* get_mutable(const ActorId& id);

 private:
  std::unordered_map<ActorId, Actor> actors_;
};

/** Greedy priority- and capacity-aware scheduler. */
class Scheduler {
 public:
  /** \param strategy If null, earliest-deadline-first is used. */
  ScheduleResult plan(const Workflow& workflow,
                      const WorkflowState& state,
                      const ActorRegistry& registry,
                      Time now,
                      const SchedulingStrategy* strategy = nullptr) const;

  /** Coroutine: yields assignments one-by-one (lazy). \param strategy If null, EDF is used. */
  Generator<Assignment> plan_lazy(const Workflow& workflow,
                                  const WorkflowState& state,
                                  const ActorRegistry& registry,
                                  Time now,
                                  const SchedulingStrategy* strategy = nullptr) const;
};

}  // namespace task_orchestrator

#endif
