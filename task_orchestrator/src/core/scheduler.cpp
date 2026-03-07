#include "task_orchestrator/core/scheduler.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <unordered_set>

#include "task_orchestrator/core/concepts.hpp"
#include "task_orchestrator/strategy/edf_strategy.hpp"

namespace task_orchestrator {

void ActorRegistry::add(Actor a) { actors_[a.id] = std::move(a); }

const Actor* ActorRegistry::get(const ActorId& id) const {
  auto it = actors_.find(id);
  return it == actors_.end() ? nullptr : &it->second;
}

std::vector<ActorId> ActorRegistry::actor_ids() const {
  std::vector<ActorId> out;
  auto keys = actors_ | std::ranges::views::transform([](const auto& kv) { return kv.first; });
  std::ranges::copy(keys, std::back_inserter(out));
  return out;
}

Actor* ActorRegistry::get_mutable(const ActorId& id) {
  auto it = actors_.find(id);
  return it == actors_.end() ? nullptr : &it->second;
}

namespace {

std::vector<TaskInfo> collect_pending_tasks(const Workflow& workflow, const WorkflowState& state) {
  std::unordered_set<TaskId> assigned(state.assigned_tasks.begin(), state.assigned_tasks.end());
  std::vector<PhaseId> ready = workflow.ready_phases(state.completed_phases);
  std::vector<TaskInfo> out;
  for (const PhaseId& ph_id : ready) {
    std::vector<TaskId> tids = workflow.task_ids_for_phase(ph_id);
    for (const TaskId& tid : tids) {
      if (assigned.count(tid)) continue;
      const Process* proc = workflow.process_for_task(tid);
      Duration dur = 1;
      Priority prio = 0;
      std::optional<Time> deadline;
      if (proc) {
        dur = proc->estimated_duration;
        prio = proc->priority;
        deadline = proc->deadline;
      }
      out.push_back({tid, ph_id, dur, prio, deadline});
    }
  }
  return out;
}

void apply_strategy(std::vector<TaskInfo>& pending, const SchedulingStrategy* strategy) {
  if (strategy) {
    strategy->order_tasks(pending);
  } else {
    EDFStrategy().order_tasks(pending);
  }
}

}  // namespace

ScheduleResult Scheduler::plan(const Workflow& workflow,
                               const WorkflowState& state,
                               const ActorRegistry& registry,
                               Time now,
                               const SchedulingStrategy* strategy) const {
  ScheduleResult result;
  result.ok = true;
  std::vector<TaskInfo> pending = collect_pending_tasks(workflow, state);
  if (pending.empty()) return result;

  apply_strategy(pending, strategy);

  std::unordered_map<ActorId, int> load = state.actor_load;
  for (const ActorId& aid : registry.actor_ids()) {
    if (load.count(aid) == 0) {
      const Actor* a = registry.get(aid);
      load[aid] = a ? a->current_load : 0;
    }
  }

  for (const TaskInfo& ti : pending) {
    Time best_start = -1;
    ActorId best_actor;
    bool found = false;
    for (const ActorId& aid : registry.actor_ids()) {
      const Actor* a = registry.get(aid);
      if (!a || load[aid] >= a->capacity) continue;
      std::optional<Time> start = a->next_available_start(now, ti.duration);
      if (!start) continue;
      if (!found || *start < best_start) {
        best_start = *start;
        best_actor = aid;
        found = true;
      }
    }
    if (!found) continue;
    result.assignments.push_back(Assignment{ti.id, best_actor, best_start});
    load[best_actor]++;
  }
  return result;
}

Generator<Assignment> Scheduler::plan_lazy(const Workflow& workflow,
                                           const WorkflowState& state,
                                           const ActorRegistry& registry,
                                           Time now,
                                           const SchedulingStrategy* strategy) const {
  std::vector<TaskInfo> pending = collect_pending_tasks(workflow, state);
  apply_strategy(pending, strategy);

  std::unordered_map<ActorId, int> load = state.actor_load;
  for (const ActorId& aid : registry.actor_ids()) {
    if (load.count(aid) == 0) {
      const Actor* a = registry.get(aid);
      load[aid] = a ? a->current_load : 0;
    }
  }

  for (const TaskInfo& ti : pending) {
    Time best_start = -1;
    ActorId best_actor;
    bool found = false;
    for (const ActorId& aid : registry.actor_ids()) {
      const Actor* a = registry.get(aid);
      if (!a || load[aid] >= a->capacity) continue;
      std::optional<Time> start = a->next_available_start(now, ti.duration);
      if (!start) continue;
      if (!found || *start < best_start) {
        best_start = *start;
        best_actor = aid;
        found = true;
      }
    }
    if (found) {
      load[best_actor]++;
      co_yield Assignment{ti.id, best_actor, best_start};
    }
  }
  co_return;
}

}  // namespace task_orchestrator
