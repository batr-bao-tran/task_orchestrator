#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__SCHEDULER_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__SCHEDULER_HPP_
#include <ranges>
#include <unordered_map>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/strategy/scheduling_strategy.hpp"
#include "utils/generator.hpp"
#include "utils/types.hpp"

namespace task_orchestrator {

enum class ActorRankingCriterion {
  EarliestFeasibleStart,
  DistanceToWork,
  UptimeUtilisation,
};

struct ActorRankingProfile {
  /** Ordered from most important to least important. */
  std::vector<ActorRankingCriterion> criteria = {
      ActorRankingCriterion::EarliestFeasibleStart,
      ActorRankingCriterion::UptimeUtilisation,
  };
};

struct Assignment {
  TaskId task_id;
  ActorId actor_id;
  Time start_time;
};

struct WorkflowState {
  std::vector<PhaseId> completed_phases;
  /** Tasks already assigned (e.g. in-flight). */
  std::vector<TaskId> assigned_tasks;
  /** Tasks that are already completed and must not be scheduled again. */
  std::vector<TaskId> completed_tasks;
  /** Per-actor current load (optional override; else use Actor::current_load). */
  std::unordered_map<ActorId, int> actor_load;
  /** Actors that cannot accept new tasks in this planning round. */
  std::vector<ActorId> unavailable_actors;
  /** Failed tasks that can be resumed by another actor. */
  std::vector<TaskId> resumable_tasks;
  /** Failed tasks that cannot be resumed yet. */
  std::vector<TaskId> unresumable_tasks;
  /** Optional map for distance-based ranking: task -> actor -> distance. */
  std::unordered_map<TaskId, std::unordered_map<ActorId, Time>> task_actor_distance;
  /** Last known assignment owner for each task. */
  std::unordered_map<TaskId, ActorId> task_actor;
  /** Last planned start and end times for task assignments. */
  std::unordered_map<TaskId, Time> task_planned_start_time;
  std::unordered_map<TaskId, Time> task_planned_end_time;
  /** Actual completion times reported by runtime execution feedback. */
  std::unordered_map<TaskId, Time> task_actual_completion_time;
};

struct ScheduleResult {
  bool ok = false;
  std::vector<Assignment> assignments;
  /** For each selected task, criterion index that decided actor ranking. */
  std::unordered_map<TaskId, size_t> ranking_decision_criterion_index;
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
  static ScheduleResult plan(const Workflow& workflow,
                             const WorkflowState& state,
                             const ActorRegistry& registry,
                             Time now,
                             const SchedulingStrategy* strategy = nullptr,
                             const ActorRankingProfile* ranking_profile = nullptr);

  /** Coroutine: yields assignments one-by-one. \param strategy If null, EDF is used. */
  static Generator<Assignment> plan_lazy(const Workflow& workflow,
                                         const WorkflowState& state,
                                         const ActorRegistry& registry,
                                         Time now,
                                         const SchedulingStrategy* strategy = nullptr,
                                         const ActorRankingProfile* ranking_profile = nullptr);
};

}  // namespace task_orchestrator

#endif
