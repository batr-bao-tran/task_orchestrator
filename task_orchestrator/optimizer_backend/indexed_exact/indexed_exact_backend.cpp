#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "task_orchestrator/optimizer/optimizer.hpp"

namespace task_orchestrator::optimizer {
namespace {

constexpr Time kPriorityOptimizationUpperBoundPerTask = 1000000;
constexpr Time kLargeTime = std::numeric_limits<Time>::max() / 4;
constexpr int kLargeCandidateLoad = std::numeric_limits<int>::max() / 4;

struct ScheduledInterval {
  Time start = 0;
  Time end = 0;
  int demand = 1;
};

struct SearchAssignment {
  OptimizedAssignment assignment;
  Priority priority = 0;
};

struct SearchState {
  std::unordered_map<ActorId, std::vector<ScheduledInterval>> reservations;
  std::unordered_map<TaskId, Time> completion_times;
  std::unordered_set<TaskId> scheduled;
  std::unordered_set<TaskId> dropped;
  std::vector<SearchAssignment> assignments;
};

const std::vector<ScheduledInterval>& reservations_for_actor(const SearchState& state, const ActorId& actor_id) {
  static const std::vector<ScheduledInterval> kNoReservations;
  const auto reservation_it = state.reservations.find(actor_id);
  return reservation_it == state.reservations.end() ? kNoReservations : reservation_it->second;
}

struct ObjectiveScore {
  size_t fulfilled = 0;
  int64_t total_priority = 0;
  Time makespan = kLargeTime;
};

struct Candidate {
  ActorId actor_id;
  Time start_time = 0;
  Time end_time = 0;
  Time distance = kLargeTime;
  bool preferred = false;
  int load = kLargeCandidateLoad;
  double execution_cost = 0.0;
  Time tardiness = 0;
};

ObjectiveScore score_for(const SearchState& state, const OptimizationOptions& options) {
  ObjectiveScore score;
  score.fulfilled = state.assignments.size();
  score.total_priority =
      std::accumulate(state.assignments.begin(),
                      state.assignments.end(),
                      int64_t{0},
                      [&options](const int64_t running_priority, const SearchAssignment& search_assignment) {
                        return running_priority +
                               (static_cast<int64_t>(search_assignment.priority) * options.objective.priority_weight);
                      });
  score.makespan = std::accumulate(state.assignments.begin(),
                                   state.assignments.end(),
                                   Time{0},
                                   [](const Time running_makespan, const SearchAssignment& search_assignment) {
                                     return std::max(running_makespan, search_assignment.assignment.end_time);
                                   });
  return score;
}

bool better_score(const ObjectiveScore& lhs, const ObjectiveScore& rhs) {
  return std::tuple(lhs.fulfilled, lhs.total_priority, -lhs.makespan) >
         std::tuple(rhs.fulfilled, rhs.total_priority, -rhs.makespan);
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

std::optional<Time> earliest_start(const OptimizerActor& actor,
                                   const std::vector<ScheduledInterval>& reservations,
                                   const Time release_time,
                                   const Duration duration,
                                   const int demand) {
  if (duration <= 0) {
    return release_time;
  }
  for (const AvailabilityWindow& window : actor.availability_windows) {
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

bool mutex_blocked(const OptimizerTask& task, const SearchState& state) {
  return std::ranges::any_of(task.mutually_exclusive_task_ids,
                             [&state](const TaskId& mutex_task_id) { return state.scheduled.contains(mutex_task_id); });
}

std::vector<const OptimizerTask*> ready_tasks(const OptimizationModel& model, const SearchState& state) {
  std::vector<const OptimizerTask*> ready;
  ready.reserve(model.tasks.size());
  for (const OptimizerTask& task : model.tasks) {
    if (state.scheduled.contains(task.id) || state.dropped.contains(task.id)) {
      continue;
    }
    if (ConstraintIndex::dependency_blocked(task, state.dropped)) {
      continue;
    }
    if (ConstraintIndex::dependencies_satisfied(task, state.scheduled, state.dropped)) {
      ready.push_back(&task);
    }
  }
  std::ranges::sort(ready, [](const OptimizerTask* lhs, const OptimizerTask* rhs) {
    const Time lhs_deadline = lhs->deadline.value_or(kLargeTime);
    const Time rhs_deadline = rhs->deadline.value_or(kLargeTime);
    return std::tuple(-lhs->priority, lhs_deadline, lhs->release_time, lhs->duration, lhs->id) <
           std::tuple(-rhs->priority, rhs_deadline, rhs->release_time, rhs->duration, rhs->id);
  });
  return ready;
}

std::vector<Candidate> candidates_for_task(const OptimizerTask& task,
                                           const ConstraintIndex& index,
                                           const SearchState& state) {
  if (mutex_blocked(task, state)) {
    return {};
  }

  const auto& eligible_actors = index.eligible_actors_for_task(task);
  std::vector<Candidate> candidates;
  candidates.reserve(eligible_actors.size());
  for (const OptimizerActor* actor : eligible_actors) {
    if (actor == nullptr || task.demand > actor->capacity) {
      continue;
    }
    if (!task.required_capabilities.empty() &&
        !std::ranges::all_of(task.required_capabilities, [&actor](const std::string& capability) {
          return std::ranges::find(actor->capabilities, capability) != actor->capabilities.end();
        })) {
      continue;
    }
    const auto& reservations = reservations_for_actor(state, actor->id);
    Time dependency_ready = 0;
    for (const TaskId& dependency_id : task.dependency_task_ids) {
      if (const auto completion_it = state.completion_times.find(dependency_id);
          completion_it != state.completion_times.end()) {
        dependency_ready = std::max(dependency_ready, completion_it->second);
      }
    }
    const Time release_time = std::max(task.release_time, dependency_ready);
    const std::optional<Time> start = earliest_start(*actor, reservations, release_time, task.duration, task.demand);
    if (!start) {
      continue;
    }
    if (task.latest_start_time && *start > *task.latest_start_time) {
      continue;
    }
    const Time end = *start + task.duration;
    if (task.deadline && end > *task.deadline) {
      continue;
    }
    const auto distance_it = task.actor_distances.find(actor->id);
    const Time distance = distance_it == task.actor_distances.end() ? kLargeTime : distance_it->second;
    const bool preferred = std::ranges::find(task.preferred_actor_ids, actor->id) != task.preferred_actor_ids.end();
    const Time tardiness = task.deadline ? std::max<Time>(0, end - *task.deadline) : 0;
    candidates.push_back(
        Candidate{.actor_id = actor->id,
                  .start_time = *start,
                  .end_time = end,
                  .distance = distance,
                  .preferred = preferred,
                  .load = concurrent_demand_at(reservations, *start),
                  .execution_cost = actor->execution_cost_per_unit * static_cast<double>(task.duration),
                  .tardiness = tardiness});
  }
  std::ranges::sort(candidates, [](const Candidate& lhs, const Candidate& rhs) {
    return std::tuple(lhs.preferred ? 0 : 1, lhs.end_time, lhs.start_time, lhs.distance, lhs.load, lhs.actor_id) <
           std::tuple(rhs.preferred ? 0 : 1, rhs.end_time, rhs.start_time, rhs.distance, rhs.load, rhs.actor_id);
  });
  return candidates;
}

void drop_descendants_if_blocked(const OptimizationModel& model, SearchState& state) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (const OptimizerTask& task : model.tasks) {
      if (state.scheduled.contains(task.id) || state.dropped.contains(task.id)) {
        continue;
      }
      if (ConstraintIndex::dependency_blocked(task, state.dropped)) {
        state.dropped.insert(task.id);
        changed = true;
      }
    }
  }
}

class IndexedBranchAndBoundBackend final : public OptimizationBackend {
 public:
  OptimizationSolution solve(const OptimizationModel& model,
                             const ConstraintIndex& index,
                             const OptimizationOptions& options) const override {
    OptimizationSolution best;
    best.ok = true;
    best.backend_name = name();
    best.stats = {};
    ObjectiveScore best_score;
    SearchState root;
    root.reservations.reserve(model.actors.size());
    for (const OptimizerActor& actor : model.actors) {
      root.reservations[actor.id] = {};
    }
    bool have_best = false;
    dfs(model, index, options, root, best, best_score, have_best);
    if (!have_best) {
      best.ok = false;
      best.error_message = "No optimization result could be produced.";
    }
    return best;
  }

  const char* name() const override { return "indexed_branch_and_bound"; }

 private:
  // NOLINTNEXTLINE(misc-no-recursion)
  static void dfs(const OptimizationModel& model,
                  const ConstraintIndex& index,
                  const OptimizationOptions& options,
                  SearchState state,
                  OptimizationSolution& best,
                  ObjectiveScore& best_score,
                  bool& have_best) {
    ++best.stats.search_nodes;
    drop_descendants_if_blocked(model, state);
    const ObjectiveScore current_score = score_for(state, options);
    const size_t remaining_possible = model.tasks.size() - state.scheduled.size() - state.dropped.size();
    if (have_best) {
      const ObjectiveScore optimistic{
          .fulfilled = current_score.fulfilled + remaining_possible,
          .total_priority = current_score.total_priority +
                            (static_cast<int64_t>(remaining_possible) * kPriorityOptimizationUpperBoundPerTask),
          .makespan = current_score.makespan,
      };
      if (!better_score(optimistic, best_score)) {
        ++best.stats.pruned_nodes;
        return;
      }
    }

    const std::vector<const OptimizerTask*> ready = ready_tasks(model, state);
    if (ready.empty()) {
      if (!have_best || better_score(current_score, best_score)) {
        best.assignments.clear();
        best.unfulfilled_task_ids.clear();
        best.assignments.reserve(state.assignments.size());
        best.unfulfilled_task_ids.reserve(model.tasks.size() - state.scheduled.size());
        for (const SearchAssignment& assignment : state.assignments) {
          best.assignments.push_back(assignment.assignment);
        }
        for (const OptimizerTask& task : model.tasks) {
          if (!state.scheduled.contains(task.id)) {
            best.unfulfilled_task_ids.push_back(task.id);
          }
        }
        best_score = current_score;
        have_best = true;
      }
      return;
    }

    const OptimizerTask& task = *ready.front();
    const std::vector<Candidate> candidates = candidates_for_task(task, index, state);
    for (const Candidate& candidate : candidates) {
      SearchState next = state;
      next.scheduled.insert(task.id);
      next.completion_times[task.id] = candidate.end_time;
      next.reservations[candidate.actor_id].push_back(
          {.start = candidate.start_time, .end = candidate.end_time, .demand = task.demand});
      next.assignments.push_back(SearchAssignment{.assignment = {.task_id = task.id,
                                                                 .actor_id = candidate.actor_id,
                                                                 .start_time = candidate.start_time,
                                                                 .end_time = candidate.end_time},
                                                  .priority = task.priority});
      dfs(model, index, options, std::move(next), best, best_score, have_best);
    }

    if (options.allow_partial_plan || !task.mandatory) {
      SearchState dropped = std::move(state);
      dropped.dropped.insert(task.id);
      dfs(model, index, options, std::move(dropped), best, best_score, have_best);
    }
  }
};

const bool kIndexedBackendRegistered = []() {
  register_backend(
      BackendDescriptor{
          .kind = BackendKind::IndexedExact,
          .name = "indexed_exact",
          .available = true,
          .optional_dependency = false,
      },
      []() { return std::make_unique<IndexedBranchAndBoundBackend>(); });
  return true;
}();

}  // namespace
}  // namespace task_orchestrator::optimizer
