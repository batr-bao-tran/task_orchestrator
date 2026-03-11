#include "task_orchestrator/core/scheduler.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
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
  std::ranges::sort(out);
  return out;
}

Actor* ActorRegistry::get_mutable(const ActorId& id) {
  auto it = actors_.find(id);
  return it == actors_.end() ? nullptr : &it->second;
}

namespace {

std::vector<TaskInfo> collect_pending_tasks(const Workflow& workflow, const WorkflowState& state) {
  std::unordered_set<TaskId> assigned(state.assigned_tasks.begin(), state.assigned_tasks.end());
  std::unordered_set<TaskId> completed(state.completed_tasks.begin(), state.completed_tasks.end());
  for (const TaskId& resumable_id : state.resumable_tasks) {
    assigned.erase(resumable_id);
  }
  std::unordered_set<TaskId> blocked(state.unresumable_tasks.begin(), state.unresumable_tasks.end());
  std::vector<PhaseId> ready = workflow.ready_phases(state.completed_phases);
  std::vector<TaskInfo> out;
  for (const PhaseId& ph_id : ready) {
    std::vector<TaskId> tids = workflow.task_ids_for_phase(ph_id);
    for (const TaskId& tid : tids) {
      if (assigned.contains(tid)) continue;
      if (completed.contains(tid)) continue;
      if (blocked.contains(tid)) continue;
      const Process* proc = workflow.process_for_task(tid);
      Duration dur = 1;
      Priority prio = 0;
      std::optional<Time> deadline;
      if (proc) {
        dur = proc->estimated_duration;
        prio = proc->priority;
        deadline = proc->deadline;
      }
      out.push_back({.id = tid, .phase_id = ph_id, .duration = dur, .priority = prio, .deadline = deadline});
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

const ActorRankingProfile& effective_ranking_profile(const ActorRankingProfile* profile) {
  static const ActorRankingProfile kDefault{};
  return profile ? *profile : kDefault;
}

struct RankedActorCandidate {
  ActorId actor_id;
  Time start_time = 0;
  Time distance_to_work = std::numeric_limits<Time>::max() / 4;
  double uptime_utilization = 0.0;
};

std::pair<int, size_t> compare_candidates(const RankedActorCandidate& lhs,
                                          const RankedActorCandidate& rhs,
                                          const ActorRankingProfile& profile) {
  size_t idx = 0;
  for (const ActorRankingCriterion criterion : profile.criteria) {
    switch (criterion) {
      case ActorRankingCriterion::EarliestFeasibleStart:
        if (lhs.start_time != rhs.start_time) {
          return {lhs.start_time < rhs.start_time ? -1 : 1, idx};
        }
        break;
      case ActorRankingCriterion::DistanceToWork:
        if (lhs.distance_to_work != rhs.distance_to_work) {
          return {lhs.distance_to_work < rhs.distance_to_work ? -1 : 1, idx};
        }
        break;
      case ActorRankingCriterion::UptimeUtilisation:
        if (lhs.uptime_utilization != rhs.uptime_utilization) {
          return {lhs.uptime_utilization > rhs.uptime_utilization ? -1 : 1, idx};
        }
        break;
    }
    ++idx;
  }
  if (lhs.actor_id != rhs.actor_id) {
    return {lhs.actor_id < rhs.actor_id ? -1 : 1, profile.criteria.size()};
  }
  return {0, profile.criteria.size()};
}

RankedActorCandidate build_candidate(const TaskInfo& ti,
                                     const ActorId& aid,
                                     const Actor& actor,
                                     Time start_time,
                                     int actor_load,
                                     const WorkflowState& state) {
  auto distance_by_task = state.task_actor_distance.find(ti.id);
  Time distance = std::numeric_limits<Time>::max() / 4;
  if (distance_by_task != state.task_actor_distance.end()) {
    auto by_actor = distance_by_task->second.find(aid);
    if (by_actor != distance_by_task->second.end()) {
      distance = by_actor->second;
    }
  }
  const int effective_capacity = std::max(actor.capacity, 1);
  const double utilization = static_cast<double>(actor_load) / static_cast<double>(effective_capacity);
  return RankedActorCandidate{
      .actor_id = aid, .start_time = start_time, .distance_to_work = distance, .uptime_utilization = utilization};
}

}  // namespace

ScheduleResult Scheduler::plan(const Workflow& workflow,
                               const WorkflowState& state,
                               const ActorRegistry& registry,
                               Time now,
                               const SchedulingStrategy* strategy,
                               const ActorRankingProfile* ranking_profile) {
  ScheduleResult result;
  result.ok = true;
  std::vector<TaskInfo> pending = collect_pending_tasks(workflow, state);
  if (pending.empty()) return result;

  apply_strategy(pending, strategy);
  const ActorRankingProfile& profile = effective_ranking_profile(ranking_profile);
  const std::unordered_set<ActorId> unavailable(state.unavailable_actors.begin(), state.unavailable_actors.end());

  std::unordered_map<ActorId, int> load = state.actor_load;
  for (const ActorId& aid : registry.actor_ids()) {
    if (!load.contains(aid)) {
      const Actor* a = registry.get(aid);
      load[aid] = a ? a->current_load : 0;
    }
  }

  for (const TaskInfo& ti : pending) {
    RankedActorCandidate best_candidate;
    bool found_best = false;
    size_t selected_reason = 0;
    for (const ActorId& aid : registry.actor_ids()) {
      if (unavailable.contains(aid)) continue;
      const Actor* a = registry.get(aid);
      if (!a || load[aid] >= a->capacity) continue;
      std::optional<Time> start = a->next_available_start(now, ti.duration);
      if (!start) continue;
      const RankedActorCandidate candidate = build_candidate(ti, aid, *a, *start, load[aid], state);
      if (!found_best) {
        best_candidate = candidate;
        found_best = true;
        selected_reason = 0;
      } else {
        auto [cmp, reason_idx] = compare_candidates(candidate, best_candidate, profile);
        if (cmp < 0) {
          best_candidate = candidate;
          selected_reason = reason_idx;
        }
      }
    }
    if (!found_best) continue;
    result.assignments.push_back(
        Assignment{.task_id = ti.id, .actor_id = best_candidate.actor_id, .start_time = best_candidate.start_time});
    result.ranking_decision_criterion_index[ti.id] = selected_reason;
    load[best_candidate.actor_id]++;
  }
  return result;
}

Generator<Assignment> Scheduler::plan_lazy(const Workflow& workflow,
                                           const WorkflowState& state,
                                           const ActorRegistry& registry,
                                           Time now,
                                           const SchedulingStrategy* strategy,
                                           const ActorRankingProfile* ranking_profile) {
  std::vector<TaskInfo> pending = collect_pending_tasks(workflow, state);
  apply_strategy(pending, strategy);
  const ActorRankingProfile& profile = effective_ranking_profile(ranking_profile);
  const std::unordered_set<ActorId> unavailable(state.unavailable_actors.begin(), state.unavailable_actors.end());

  std::unordered_map<ActorId, int> load = state.actor_load;
  for (const ActorId& aid : registry.actor_ids()) {
    if (!load.contains(aid)) {
      const Actor* a = registry.get(aid);
      load[aid] = a ? a->current_load : 0;
    }
  }

  for (const TaskInfo& ti : pending) {
    RankedActorCandidate best_candidate;
    bool found_best = false;
    for (const ActorId& aid : registry.actor_ids()) {
      if (unavailable.contains(aid)) continue;
      const Actor* a = registry.get(aid);
      if (!a || load[aid] >= a->capacity) continue;
      std::optional<Time> start = a->next_available_start(now, ti.duration);
      if (!start) continue;
      const RankedActorCandidate candidate = build_candidate(ti, aid, *a, *start, load[aid], state);
      if (!found_best) {
        best_candidate = candidate;
        found_best = true;
      } else {
        auto [cmp, reason_idx] = compare_candidates(candidate, best_candidate, profile);
        (void)reason_idx;
        if (cmp < 0) {
          best_candidate = candidate;
        }
      }
    }
    if (found_best) {
      ++load[best_candidate.actor_id];
      Assignment a{.task_id = ti.id, .actor_id = best_candidate.actor_id, .start_time = best_candidate.start_time};
      co_yield a;
    }
  }
  co_return;
}

}  // namespace task_orchestrator
