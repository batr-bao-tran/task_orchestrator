#include "task_orchestrator/core/scheduler.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
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

constexpr Duration kDefaultTaskDuration = 1;
constexpr Priority kDefaultTaskPriority = 0;
constexpr Time kDefaultTaskReleaseTime = 0;
// Headroom for infinity
constexpr Time kLargeRankingTime = std::numeric_limits<Time>::max() / 4;
constexpr int kLargeRankingLoad = std::numeric_limits<int>::max() / 4;

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
      Duration dur = kDefaultTaskDuration;
      Priority prio = kDefaultTaskPriority;
      Time release_time = kDefaultTaskReleaseTime;
      std::optional<Time> deadline;
      if (proc) {
        dur = proc->estimated_duration;
        prio = proc->priority;
        deadline = proc->deadline;
      }
      if (const auto release_it = state.task_release_time.find(tid); release_it != state.task_release_time.end()) {
        release_time = release_it->second;
      }
      out.push_back({.id = tid,
                     .phase_id = ph_id,
                     .duration = dur,
                     .priority = prio,
                     .release_time = release_time,
                     .deadline = deadline});
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
  Time finish_time = 0;
  Time distance_to_work = kLargeRankingTime;
  double uptime_utilization = 0.0;
  int actor_load = kLargeRankingLoad;
  bool preferred_actor = false;
};

struct CandidateSelection {
  RankedActorCandidate candidate;
  size_t ranking_reason_index = 0;
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
      case ActorRankingCriterion::EarliestFeasibleCompletion:
        if (lhs.finish_time != rhs.finish_time) {
          return {lhs.finish_time < rhs.finish_time ? -1 : 1, idx};
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
      case ActorRankingCriterion::LeastLoaded:
        if (lhs.actor_load != rhs.actor_load) {
          return {lhs.actor_load < rhs.actor_load ? -1 : 1, idx};
        }
        break;
      case ActorRankingCriterion::PreferredActor:
        if (lhs.preferred_actor != rhs.preferred_actor) {
          return {lhs.preferred_actor ? -1 : 1, idx};
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
  Time distance = kLargeRankingTime;
  if (distance_by_task != state.task_actor_distance.end()) {
    auto by_actor = distance_by_task->second.find(aid);
    if (by_actor != distance_by_task->second.end()) {
      distance = by_actor->second;
    }
  }
  bool preferred_actor = false;
  if (const auto preferred_it = state.task_preferred_actors.find(ti.id);
      preferred_it != state.task_preferred_actors.end()) {
    preferred_actor = std::ranges::find(preferred_it->second, aid) != preferred_it->second.end();
  }
  const int effective_capacity = std::max(actor.capacity, 1);
  const double utilization = static_cast<double>(actor_load) / static_cast<double>(effective_capacity);
  return RankedActorCandidate{.actor_id = aid,
                              .start_time = start_time,
                              .finish_time = start_time + ti.duration,
                              .distance_to_work = distance,
                              .uptime_utilization = utilization,
                              .actor_load = actor_load,
                              .preferred_actor = preferred_actor};
}

bool actor_is_allowed(const WorkflowState& state, const TaskId& task_id, const ActorId& actor_id) {
  const auto allow_it = state.task_allowed_actors.find(task_id);
  if (allow_it == state.task_allowed_actors.end() || allow_it->second.empty()) {
    return true;
  }
  return std::ranges::find(allow_it->second, actor_id) != allow_it->second.end();
}

std::unordered_map<ActorId, int> initialize_actor_loads(const ActorRegistry& registry,
                                                        const std::vector<ActorId>& sorted_actor_ids,
                                                        const WorkflowState& state) {
  std::unordered_map<ActorId, int> actor_load_by_id = state.actor_load;
  actor_load_by_id.reserve(sorted_actor_ids.size());
  for (const ActorId& actor_id : sorted_actor_ids) {
    if (actor_load_by_id.contains(actor_id)) {
      continue;
    }
    const Actor* actor = registry.get(actor_id);
    actor_load_by_id.emplace(actor_id, actor ? actor->current_load : 0);
  }
  return actor_load_by_id;
}

std::optional<CandidateSelection> select_best_candidate(const TaskInfo& task_info,
                                                        const std::vector<ActorId>& sorted_actor_ids,
                                                        const std::unordered_set<ActorId>& unavailable_actor_ids,
                                                        const ActorRegistry& registry,
                                                        const WorkflowState& state,
                                                        Time now,
                                                        const ActorRankingProfile& profile,
                                                        const std::unordered_map<ActorId, int>& actor_load_by_id) {
  std::optional<CandidateSelection> best_selection;
  for (const ActorId& actor_id : sorted_actor_ids) {
    if (unavailable_actor_ids.contains(actor_id)) {
      continue;
    }
    if (!actor_is_allowed(state, task_info.id, actor_id)) {
      continue;
    }
    const Actor* actor = registry.get(actor_id);
    if (actor == nullptr) {
      continue;
    }
    const int current_actor_load = actor_load_by_id.at(actor_id);
    if (current_actor_load >= actor->capacity) {
      continue;
    }
    const std::optional<Time> start_time =
        actor->next_available_start(std::max(now, task_info.release_time), task_info.duration);
    if (!start_time) {
      continue;
    }
    if (task_info.deadline && (*start_time + task_info.duration) > *task_info.deadline) {
      continue;
    }
    const RankedActorCandidate candidate =
        build_candidate(task_info, actor_id, *actor, *start_time, current_actor_load, state);
    if (!best_selection) {
      best_selection = CandidateSelection{.candidate = candidate, .ranking_reason_index = 0};
      continue;
    }
    const auto [comparison_result, ranking_reason_index] =
        compare_candidates(candidate, best_selection->candidate, profile);
    if (comparison_result < 0) {
      best_selection = CandidateSelection{.candidate = candidate, .ranking_reason_index = ranking_reason_index};
    }
  }
  return best_selection;
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
  const std::vector<ActorId> sorted_actor_ids = registry.actor_ids();
  const std::unordered_set<ActorId> unavailable_actor_ids(state.unavailable_actors.begin(),
                                                          state.unavailable_actors.end());
  std::unordered_map<ActorId, int> actor_load_by_id = initialize_actor_loads(registry, sorted_actor_ids, state);

  for (const TaskInfo& task_info : pending) {
    const std::optional<CandidateSelection> best_selection = select_best_candidate(
        task_info, sorted_actor_ids, unavailable_actor_ids, registry, state, now, profile, actor_load_by_id);
    if (!best_selection) {
      continue;
    }
    result.assignments.push_back(Assignment{.task_id = task_info.id,
                                            .actor_id = best_selection->candidate.actor_id,
                                            .start_time = best_selection->candidate.start_time});
    result.ranking_decision_criterion_index[task_info.id] = best_selection->ranking_reason_index;
    ++actor_load_by_id[best_selection->candidate.actor_id];
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
  const std::vector<ActorId> sorted_actor_ids = registry.actor_ids();
  const std::unordered_set<ActorId> unavailable_actor_ids(state.unavailable_actors.begin(),
                                                          state.unavailable_actors.end());
  std::unordered_map<ActorId, int> actor_load_by_id = initialize_actor_loads(registry, sorted_actor_ids, state);

  for (const TaskInfo& task_info : pending) {
    const std::optional<CandidateSelection> best_selection = select_best_candidate(
        task_info, sorted_actor_ids, unavailable_actor_ids, registry, state, now, profile, actor_load_by_id);
    if (best_selection) {
      ++actor_load_by_id[best_selection->candidate.actor_id];
      co_yield Assignment{
          .task_id = task_info.id,
          .actor_id = best_selection->candidate.actor_id,
          .start_time = best_selection->candidate.start_time,
      };
    }
  }
  co_return;
}

}  // namespace task_orchestrator
