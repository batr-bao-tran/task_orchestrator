#include "task_orchestrator/core/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <tuple>
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
constexpr int kDefaultTaskDemand = 1;
constexpr Time kUnknownDistancePadding = 1;
constexpr Time kLargeRankingTime = std::numeric_limits<Time>::max() / 4;
constexpr Time kOpaqueReservationEnd = std::numeric_limits<Time>::max() / 4;
constexpr int kLargeRankingLoad = std::numeric_limits<int>::max() / 4;
constexpr double kLargeExecutionCost = static_cast<double>(kLargeRankingTime);

struct PendingTask {
  TaskInfo info;
  const Process* process = nullptr;
};

struct ScheduledInterval {
  Time start = 0;
  Time end = 0;
  int demand = 1;
};

struct RankedActorCandidate {
  ActorId actor_id;
  Time start_time = 0;
  Time finish_time = 0;
  Time distance_to_work = kLargeRankingTime;
  double execution_cost = kLargeExecutionCost;
  double uptime_utilization = 0.0;
  int actor_load = kLargeRankingLoad;
  bool preferred_actor = false;
};

struct CandidateSelection {
  RankedActorCandidate candidate;
  size_t ranking_reason_index = 0;
};

using ReservationTable = std::unordered_map<ActorId, std::vector<ScheduledInterval>>;
using CompletionTable = std::unordered_map<TaskId, Time>;

Time fallback_distance(const std::unordered_map<ActorId, Time>& actor_distances) {
  if (actor_distances.empty()) {
    return 0;
  }
  const auto farthest = std::ranges::max_element(actor_distances, {}, [](const auto& entry) { return entry.second; });
  return farthest == actor_distances.end() ? 0 : farthest->second + kUnknownDistancePadding;
}

std::vector<PendingTask> collect_pending_tasks(const Workflow& workflow, const WorkflowState& state) {
  std::unordered_set<TaskId> assigned(state.assigned_tasks.begin(), state.assigned_tasks.end());
  std::unordered_set<TaskId> completed(state.completed_tasks.begin(), state.completed_tasks.end());
  for (const TaskId& resumable_id : state.resumable_tasks) {
    assigned.erase(resumable_id);
  }
  std::unordered_set<TaskId> blocked(state.unresumable_tasks.begin(), state.unresumable_tasks.end());
  std::vector<PhaseId> ready = workflow.ready_phases(state.completed_phases);
  std::vector<PendingTask> out;
  for (const PhaseId& phase_id : ready) {
    std::vector<TaskId> task_ids = workflow.task_ids_for_phase(phase_id);
    for (const TaskId& task_id : task_ids) {
      if (assigned.contains(task_id)) continue;
      if (completed.contains(task_id)) continue;
      if (blocked.contains(task_id)) continue;
      const Process* process = workflow.process_for_task(task_id);
      const Duration duration = process ? process->estimated_duration : kDefaultTaskDuration;
      const Priority priority = process ? process->priority : kDefaultTaskPriority;
      const Time release_time = process ? process->release_time : kDefaultTaskReleaseTime;
      const std::optional<Time> deadline = process ? process->deadline : std::nullopt;
      const std::optional<Time> latest_start_time = process ? process->latest_start_time : std::nullopt;
      const int demand = process ? process->demand : kDefaultTaskDemand;
      const size_t dependency_count = process ? process->dependency_task_ids.size() : 0U;
      out.push_back(PendingTask{
          .info =
              TaskInfo{
                  .id = task_id,
                  .phase_id = phase_id,
                  .duration = duration,
                  .priority = priority,
                  .release_time = release_time,
                  .deadline = deadline,
                  .latest_start_time = latest_start_time,
                  .demand = demand,
                  .dependency_count = dependency_count,
              },
          .process = process,
      });
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

std::optional<std::string> validate_supported_tasks(const std::vector<PendingTask>& pending_tasks) {
  const auto preemptible_it = std::ranges::find_if(pending_tasks, [](const PendingTask& pending_task) {
    return pending_task.process != nullptr && pending_task.process->preemptible;
  });
  if (preemptible_it == pending_tasks.end()) {
    return std::nullopt;
  }
  return "Preemptible tasks are not yet supported by the runtime scheduler: " + preemptible_it->info.id;
}

const std::vector<ActorId>* effective_allowed_actors(const PendingTask& task, const WorkflowState& state) {
  const auto override_it = state.task_allowed_actors.find(task.info.id);
  if (override_it != state.task_allowed_actors.end()) {
    return &override_it->second;
  }
  if (task.process == nullptr || task.process->allowed_actor_ids.empty()) {
    return nullptr;
  }
  return &task.process->allowed_actor_ids;
}

const std::vector<ActorId>* effective_preferred_actors(const PendingTask& task, const WorkflowState& state) {
  const auto override_it = state.task_preferred_actors.find(task.info.id);
  if (override_it != state.task_preferred_actors.end()) {
    return &override_it->second;
  }
  if (task.process == nullptr || task.process->preferred_actor_ids.empty()) {
    return nullptr;
  }
  return &task.process->preferred_actor_ids;
}

Time effective_release_time(const PendingTask& task, const WorkflowState& state) {
  const auto release_it = state.task_release_time.find(task.info.id);
  return release_it != state.task_release_time.end() ? release_it->second : task.info.release_time;
}

std::optional<Time> effective_latest_start_time(const PendingTask& task) { return task.info.latest_start_time; }

bool actor_matches_type_constraints(const PendingTask& task, const Actor& actor) {
  return task.process == nullptr || task.process->allowed_actor_types.empty() ||
         std::ranges::find(task.process->allowed_actor_types, actor.type) != task.process->allowed_actor_types.end();
}

bool actor_supports_required_capabilities(const PendingTask& task, const Actor& actor) {
  return task.process == nullptr ||
         std::ranges::all_of(task.process->required_capabilities, [&actor](const std::string& capability) {
           return std::ranges::find(actor.capabilities, capability) != actor.capabilities.end();
         });
}

bool actor_is_allowed(const PendingTask& task, const Actor& actor, const WorkflowState& state) {
  const std::vector<ActorId>* allowed_actor_ids = effective_allowed_actors(task, state);
  if (allowed_actor_ids != nullptr && !allowed_actor_ids->empty() &&
      std::ranges::find(*allowed_actor_ids, actor.id) == allowed_actor_ids->end()) {
    return false;
  }
  if (!actor_matches_type_constraints(task, actor)) {
    return false;
  }
  if (!actor_supports_required_capabilities(task, actor)) {
    return false;
  }
  return task.info.demand <= actor.capacity;
}

int concurrent_demand_at(const std::vector<ScheduledInterval>& reservations, const Time instant) {
  return std::accumulate(reservations.begin(),
                         reservations.end(),
                         0,
                         [instant](const int running_demand, const ScheduledInterval& reservation) {
                           return reservation.start <= instant && instant < reservation.end
                                      ? running_demand + reservation.demand
                                      : running_demand;
                         });
}

bool has_capacity_for_interval(const std::vector<ScheduledInterval>& reservations,
                               const int capacity,
                               const Time start,
                               const Time end,
                               const int demand) {
  for (Time instant = start; instant < end; ++instant) {
    if (concurrent_demand_at(reservations, instant) + demand > capacity) {
      return false;
    }
  }
  return true;
}

std::optional<Time> earliest_start(const Actor& actor,
                                   const std::vector<ScheduledInterval>& reservations,
                                   const Time release_time,
                                   const Duration duration,
                                   const int demand) {
  if (duration <= 0) {
    return release_time;
  }
  std::vector<AvailabilityWindow> sorted_windows(actor.availability_windows.begin(), actor.availability_windows.end());
  std::ranges::sort(sorted_windows, {}, &AvailabilityWindow::start);
  for (const AvailabilityWindow& window : sorted_windows) {
    const Time window_start = std::max(window.start, release_time);
    const Time latest_start = window.end - duration;
    if (window_start > latest_start) {
      continue;
    }
    for (Time candidate = window_start; candidate <= latest_start; ++candidate) {
      if (has_capacity_for_interval(reservations, actor.capacity, candidate, candidate + duration, demand)) {
        return candidate;
      }
    }
  }
  return std::nullopt;
}

Time effective_distance_to_work(const PendingTask& task, const ActorId& actor_id, const WorkflowState& state) {
  const auto override_it = state.task_actor_distance.find(task.info.id);
  if (override_it != state.task_actor_distance.end()) {
    const auto actor_distance_it = override_it->second.find(actor_id);
    return actor_distance_it != override_it->second.end() ? actor_distance_it->second
                                                          : fallback_distance(override_it->second);
  }

  if (task.process == nullptr) {
    return 0;
  }
  const auto actor_distance_it = task.process->actor_distances.find(actor_id);
  return actor_distance_it != task.process->actor_distances.end() ? actor_distance_it->second
                                                                  : fallback_distance(task.process->actor_distances);
}

bool preferred_actor_for_task(const PendingTask& task, const ActorId& actor_id, const WorkflowState& state) {
  const std::vector<ActorId>* preferred_actors = effective_preferred_actors(task, state);
  return preferred_actors != nullptr && std::ranges::find(*preferred_actors, actor_id) != preferred_actors->end();
}

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
      case ActorRankingCriterion::ExecutionCost:
        if (lhs.execution_cost != rhs.execution_cost) {
          return {lhs.execution_cost < rhs.execution_cost ? -1 : 1, idx};
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

RankedActorCandidate build_candidate(const PendingTask& task,
                                     const Actor& actor,
                                     const Time start_time,
                                     const WorkflowState& state,
                                     const std::vector<ScheduledInterval>& reservations) {
  const int actor_load = concurrent_demand_at(reservations, start_time);
  const int effective_capacity = std::max(actor.capacity, 1);
  const double utilization = static_cast<double>(actor_load) / static_cast<double>(effective_capacity);
  return RankedActorCandidate{
      .actor_id = actor.id,
      .start_time = start_time,
      .finish_time = start_time + task.info.duration,
      .distance_to_work = effective_distance_to_work(task, actor.id, state),
      .execution_cost = actor.execution_cost_per_unit * static_cast<double>(task.info.duration),
      .uptime_utilization = utilization,
      .actor_load = actor_load,
      .preferred_actor = preferred_actor_for_task(task, actor.id, state),
  };
}

ReservationTable initialize_actor_reservations(const Workflow& workflow,
                                               const ActorRegistry& registry,
                                               const std::vector<ActorId>& sorted_actor_ids,
                                               const WorkflowState& state,
                                               const Time now) {
  ReservationTable reservations;
  reservations.reserve(sorted_actor_ids.size());
  for (const ActorId& actor_id : sorted_actor_ids) {
    reservations.emplace(actor_id, std::vector<ScheduledInterval>{});
  }

  for (const TaskId& task_id : state.assigned_tasks) {
    const auto actor_it = state.task_actor.find(task_id);
    if (actor_it == state.task_actor.end()) {
      continue;
    }
    const Process* process = workflow.process_for_task(task_id);
    const Duration duration = process ? process->estimated_duration : kDefaultTaskDuration;
    const int demand = process ? process->demand : kDefaultTaskDemand;
    const Time start_time =
        state.task_planned_start_time.contains(task_id) ? state.task_planned_start_time.at(task_id) : now;
    const Time end_time = state.task_planned_end_time.contains(task_id) ? state.task_planned_end_time.at(task_id)
                                                                        : std::max(start_time, now) + duration;
    if (end_time <= start_time) {
      continue;
    }
    reservations[actor_it->second].push_back(ScheduledInterval{.start = start_time, .end = end_time, .demand = demand});
  }

  for (const ActorId& actor_id : sorted_actor_ids) {
    const Actor* actor = registry.get(actor_id);
    int baseline_load = 0;
    if (state.actor_load.contains(actor_id)) {
      baseline_load = state.actor_load.at(actor_id);
    } else if (actor != nullptr) {
      baseline_load = actor->current_load;
    }
    const int known_current_demand = concurrent_demand_at(reservations[actor_id], now);
    const int opaque_demand = std::max(0, baseline_load - known_current_demand);
    if (opaque_demand > 0) {
      reservations[actor_id].push_back(
          ScheduledInterval{.start = now, .end = kOpaqueReservationEnd, .demand = opaque_demand});
    }
  }
  return reservations;
}

CompletionTable initialize_completion_times(const Workflow& workflow, const WorkflowState& state, const Time now) {
  CompletionTable completion_times;
  completion_times.reserve(state.completed_tasks.size() + state.assigned_tasks.size());

  for (const TaskId& task_id : state.completed_tasks) {
    if (const auto actual_it = state.task_actual_completion_time.find(task_id);
        actual_it != state.task_actual_completion_time.end()) {
      completion_times[task_id] = actual_it->second;
      continue;
    }
    if (const auto planned_it = state.task_planned_end_time.find(task_id);
        planned_it != state.task_planned_end_time.end()) {
      completion_times[task_id] = planned_it->second;
      continue;
    }
    completion_times[task_id] = now;
  }

  for (const TaskId& task_id : state.assigned_tasks) {
    if (const auto planned_it = state.task_planned_end_time.find(task_id);
        planned_it != state.task_planned_end_time.end()) {
      completion_times[task_id] = planned_it->second;
      continue;
    }
    const Process* process = workflow.process_for_task(task_id);
    completion_times[task_id] = now + (process ? process->estimated_duration : kDefaultTaskDuration);
  }
  return completion_times;
}

std::optional<Time> dependency_ready_time(const PendingTask& task, const CompletionTable& completion_times) {
  if (task.process == nullptr) {
    return Time{0};
  }
  Time ready_time = 0;
  for (const TaskId& dependency_id : task.process->dependency_task_ids) {
    const auto completion_it = completion_times.find(dependency_id);
    if (completion_it == completion_times.end()) {
      return std::nullopt;
    }
    ready_time = std::max(ready_time, completion_it->second);
  }
  return ready_time;
}

bool mutex_conflict(const PendingTask& task, const std::unordered_set<TaskId>& blocked_task_ids) {
  return task.process != nullptr && std::ranges::any_of(task.process->mutually_exclusive_task_ids,
                                                        [&blocked_task_ids](const TaskId& other_task_id) {
                                                          return blocked_task_ids.contains(other_task_id);
                                                        });
}

void order_ready_tasks(std::vector<const PendingTask*>& ready_tasks, const SchedulingStrategy* strategy) {
  std::vector<TaskInfo> ordered_task_info;
  ordered_task_info.reserve(ready_tasks.size());
  std::ranges::transform(ready_tasks, std::back_inserter(ordered_task_info), [](const PendingTask* pending_task) {
    return pending_task->info;
  });
  apply_strategy(ordered_task_info, strategy);

  std::unordered_map<TaskId, size_t> order_by_task_id;
  order_by_task_id.reserve(ordered_task_info.size());
  for (size_t index = 0; index < ordered_task_info.size(); ++index) {
    order_by_task_id.emplace(ordered_task_info[index].id, index);
  }

  std::ranges::sort(ready_tasks, [&order_by_task_id](const PendingTask* lhs, const PendingTask* rhs) {
    return std::tuple(order_by_task_id.at(lhs->info.id), lhs->info.id) <
           std::tuple(order_by_task_id.at(rhs->info.id), rhs->info.id);
  });
}

std::vector<const PendingTask*> collect_ready_tasks(const std::unordered_set<TaskId>& remaining_task_ids,
                                                    const std::unordered_map<TaskId, const PendingTask*>& pending_by_id,
                                                    const CompletionTable& completion_times,
                                                    const std::unordered_set<TaskId>& blocked_task_ids,
                                                    const SchedulingStrategy* strategy) {
  std::vector<const PendingTask*> ready_tasks;
  ready_tasks.reserve(remaining_task_ids.size());
  for (const TaskId& task_id : remaining_task_ids) {
    const PendingTask& task = *pending_by_id.at(task_id);
    if (mutex_conflict(task, blocked_task_ids)) {
      continue;
    }
    if (!dependency_ready_time(task, completion_times).has_value()) {
      continue;
    }
    ready_tasks.push_back(&task);
  }
  order_ready_tasks(ready_tasks, strategy);
  return ready_tasks;
}

std::optional<CandidateSelection> select_best_candidate(const PendingTask& task,
                                                        const std::vector<ActorId>& sorted_actor_ids,
                                                        const std::unordered_set<ActorId>& unavailable_actor_ids,
                                                        const ActorRegistry& registry,
                                                        const WorkflowState& state,
                                                        const ReservationTable& reservations,
                                                        const CompletionTable& completion_times,
                                                        const Time now,
                                                        const ActorRankingProfile& profile) {
  const std::optional<Time> dependency_ready = dependency_ready_time(task, completion_times);
  if (!dependency_ready.has_value()) {
    return std::nullopt;
  }

  const Time earliest_release = std::max({now, effective_release_time(task, state), *dependency_ready});
  std::optional<CandidateSelection> best_selection;
  for (const ActorId& actor_id : sorted_actor_ids) {
    if (unavailable_actor_ids.contains(actor_id)) {
      continue;
    }
    const Actor* actor = registry.get(actor_id);
    if (actor == nullptr) {
      continue;
    }
    if (!actor_is_allowed(task, *actor, state)) {
      continue;
    }

    const auto reservation_it = reservations.find(actor_id);
    const std::vector<ScheduledInterval>& actor_reservations =
        reservation_it == reservations.end() ? std::vector<ScheduledInterval>{} : reservation_it->second;
    const std::optional<Time> start_time =
        earliest_start(*actor, actor_reservations, earliest_release, task.info.duration, task.info.demand);
    if (!start_time) {
      continue;
    }
    if (const std::optional<Time> latest_start = effective_latest_start_time(task);
        latest_start && *start_time > *latest_start) {
      continue;
    }
    if (task.info.deadline && (*start_time + task.info.duration) > *task.info.deadline) {
      continue;
    }

    const RankedActorCandidate candidate = build_candidate(task, *actor, *start_time, state, actor_reservations);
    if (!best_selection.has_value()) {
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

  std::vector<PendingTask> pending_tasks = collect_pending_tasks(workflow, state);
  if (pending_tasks.empty()) {
    return result;
  }

  if (const std::optional<std::string> validation_error = validate_supported_tasks(pending_tasks);
      validation_error.has_value()) {
    result.ok = false;
    result.error_message = *validation_error;
    return result;
  }

  const ActorRankingProfile& profile = effective_ranking_profile(ranking_profile);
  const std::vector<ActorId> sorted_actor_ids = registry.actor_ids();
  const std::unordered_set<ActorId> unavailable_actor_ids(state.unavailable_actors.begin(),
                                                          state.unavailable_actors.end());
  ReservationTable reservations = initialize_actor_reservations(workflow, registry, sorted_actor_ids, state, now);
  CompletionTable completion_times = initialize_completion_times(workflow, state, now);
  std::unordered_set<TaskId> blocked_task_ids(state.completed_tasks.begin(), state.completed_tasks.end());
  blocked_task_ids.insert(state.assigned_tasks.begin(), state.assigned_tasks.end());

  std::unordered_map<TaskId, const PendingTask*> pending_by_id;
  pending_by_id.reserve(pending_tasks.size());
  std::unordered_set<TaskId> remaining_task_ids;
  remaining_task_ids.reserve(pending_tasks.size());
  for (const PendingTask& pending_task : pending_tasks) {
    pending_by_id.emplace(pending_task.info.id, &pending_task);
    remaining_task_ids.insert(pending_task.info.id);
  }

  while (!remaining_task_ids.empty()) {
    const std::vector<const PendingTask*> ready_tasks =
        collect_ready_tasks(remaining_task_ids, pending_by_id, completion_times, blocked_task_ids, strategy);
    if (ready_tasks.empty()) {
      break;
    }

    bool progress = false;
    for (const PendingTask* ready_task : ready_tasks) {
      if (ready_task == nullptr || !remaining_task_ids.contains(ready_task->info.id)) {
        continue;
      }
      if (mutex_conflict(*ready_task, blocked_task_ids)) {
        remaining_task_ids.erase(ready_task->info.id);
        continue;
      }

      const std::optional<CandidateSelection> best_selection = select_best_candidate(*ready_task,
                                                                                     sorted_actor_ids,
                                                                                     unavailable_actor_ids,
                                                                                     registry,
                                                                                     state,
                                                                                     reservations,
                                                                                     completion_times,
                                                                                     now,
                                                                                     profile);
      if (!best_selection.has_value()) {
        remaining_task_ids.erase(ready_task->info.id);
        continue;
      }

      result.assignments.push_back(Assignment{
          .task_id = ready_task->info.id,
          .actor_id = best_selection->candidate.actor_id,
          .start_time = best_selection->candidate.start_time,
      });
      result.ranking_decision_criterion_index[ready_task->info.id] = best_selection->ranking_reason_index;
      reservations[best_selection->candidate.actor_id].push_back(ScheduledInterval{
          .start = best_selection->candidate.start_time,
          .end = best_selection->candidate.finish_time,
          .demand = ready_task->info.demand,
      });
      completion_times[ready_task->info.id] = best_selection->candidate.finish_time;
      blocked_task_ids.insert(ready_task->info.id);
      remaining_task_ids.erase(ready_task->info.id);
      progress = true;
    }

    if (!progress) {
      break;
    }
  }

  std::ranges::sort(result.assignments, [](const Assignment& lhs, const Assignment& rhs) {
    return std::tuple(lhs.start_time, lhs.task_id, lhs.actor_id) <
           std::tuple(rhs.start_time, rhs.task_id, rhs.actor_id);
  });
  return result;
}

Generator<Assignment> Scheduler::plan_lazy(const Workflow& workflow,
                                           const WorkflowState& state,
                                           const ActorRegistry& registry,
                                           Time now,
                                           const SchedulingStrategy* strategy,
                                           const ActorRankingProfile* ranking_profile) {
  const ScheduleResult schedule = plan(workflow, state, registry, now, strategy, ranking_profile);
  for (const Assignment& assignment : schedule.assignments) {
    co_yield assignment;
  }
  co_return;
}

}  // namespace task_orchestrator
