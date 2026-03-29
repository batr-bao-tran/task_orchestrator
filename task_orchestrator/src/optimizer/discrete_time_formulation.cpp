#include "task_orchestrator/optimizer/discrete_time_formulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace task_orchestrator::optimizer {
namespace {

int64_t scale_double(const double value) {
  return static_cast<int64_t>(std::llround(value * static_cast<double>(kScaledDoubleFactor)));
}

bool task_capabilities_supported(const OptimizerTask& task, const OptimizerActor& actor) {
  return std::ranges::all_of(task.required_capabilities, [&actor](const std::string& capability) {
    return std::ranges::find(actor.capabilities, capability) != actor.capabilities.end();
  });
}

Time fallback_distance_for_task(const OptimizerTask& task) {
  if (task.actor_distances.empty()) {
    return 0;
  }
  const auto farthest =
      std::ranges::max_element(task.actor_distances, {}, [](const auto& entry) { return entry.second; });
  return farthest == task.actor_distances.end() ? 0 : farthest->second + kUnknownDistancePadding;
}

int64_t option_objective_coefficient(const OptimizerTask& task,
                                     const OptimizerActor& actor,
                                     const OptimizationOptions& options,
                                     const Time start_time,
                                     const Time end_time) {
  const bool preferred_actor = std::ranges::find(task.preferred_actor_ids, actor.id) != task.preferred_actor_ids.end();
  const Time fallback_distance = fallback_distance_for_task(task);
  const auto distance_it = task.actor_distances.find(actor.id);
  const Time distance = distance_it == task.actor_distances.end() ? fallback_distance : distance_it->second;
  const Time tardiness = task.deadline ? std::max<Time>(0, end_time - *task.deadline) : 0;
  const int64_t scaled_execution_cost =
      scale_double(actor.execution_cost_per_unit * static_cast<double>(task.duration));
  const int64_t scaled_tardiness_cost = scale_double(task.tardiness_cost_per_unit);
  const int64_t scaled_early_start_bonus = scale_double(task.early_start_bonus);

  int64_t coefficient = 0;
  coefficient += options.objective.fulfilled_task_weight;
  coefficient += static_cast<int64_t>(task.priority) * options.objective.priority_weight;
  coefficient += static_cast<int64_t>(distance) * options.objective.travel_distance_weight;
  coefficient += static_cast<int64_t>(tardiness) * options.objective.tardiness_weight;
  coefficient += scaled_execution_cost * options.objective.execution_cost_weight;
  coefficient -= scaled_tardiness_cost * static_cast<int64_t>(tardiness);
  coefficient -= scaled_early_start_bonus * static_cast<int64_t>(start_time);
  if (preferred_actor) {
    coefficient += options.objective.preferred_actor_weight;
  }
  return coefficient;
}

std::optional<Time> latest_feasible_start(const OptimizerTask& task, const AvailabilityWindow& window) {
  if (window.end < task.duration) {
    return std::nullopt;
  }
  Time latest_start = window.end - task.duration;
  if (task.latest_start_time) {
    latest_start = std::min(latest_start, *task.latest_start_time);
  }
  if (task.deadline) {
    latest_start = std::min(latest_start, *task.deadline - task.duration);
  }
  return latest_start;
}

}  // namespace

bool backend_model_supported(const OptimizationModel& model, std::string* error_message) {
  const auto preemptible_it =
      std::ranges::find_if(model.tasks, [](const OptimizerTask& task) { return task.preemptible; });
  if (preemptible_it == model.tasks.end()) {
    return true;
  }
  if (error_message) {
    *error_message = "Preemptible tasks are not yet supported by the discrete-time solver backends.";
  }
  return false;
}

DiscreteTimeFormulation build_discrete_time_formulation(const OptimizationModel& model,
                                                        const ConstraintIndex& index,
                                                        const OptimizationOptions& options) {
  DiscreteTimeFormulation formulation;
  formulation.model = &model;
  formulation.task_decisions.reserve(model.tasks.size());

  std::unordered_map<TaskId, size_t> task_indexes;
  task_indexes.reserve(model.tasks.size());
  for (size_t task_index = 0; task_index < model.tasks.size(); ++task_index) {
    task_indexes.emplace(model.tasks[task_index].id, task_index);
  }

  std::map<std::pair<size_t, Time>, std::vector<size_t>> slot_option_indexes;
  std::set<std::pair<size_t, size_t>> mutex_pairs;

  for (size_t task_index = 0; task_index < model.tasks.size(); ++task_index) {
    const OptimizerTask& task = model.tasks[task_index];
    DiscreteTaskDecision decision{
        .task_index = task_index,
        .task_id = task.id,
        .required = !options.allow_partial_plan && task.mandatory,
        .option_indexes = {},
    };

    for (const OptimizerActor* actor : index.eligible_actors_for_task(task)) {
      if (actor == nullptr || task.demand > actor->capacity || !task_capabilities_supported(task, *actor)) {
        continue;
      }

      const auto actor_it = std::ranges::find(model.actors, actor->id, &OptimizerActor::id);
      if (actor_it == model.actors.end()) {
        continue;
      }
      const auto actor_index = static_cast<size_t>(std::distance(model.actors.begin(), actor_it));

      for (const AvailabilityWindow& window : actor->availability_windows) {
        const Time first_start = std::max(window.start, task.release_time);
        const std::optional<Time> last_start = latest_feasible_start(task, window);
        if (!last_start || first_start > *last_start) {
          continue;
        }

        for (Time start_time = first_start; start_time <= *last_start; ++start_time) {
          const Time end_time = start_time + task.duration;
          const size_t option_index = formulation.options.size();
          formulation.options.push_back(DiscreteOption{
              .option_index = option_index,
              .task_index = task_index,
              .actor_index = actor_index,
              .task_id = task.id,
              .actor_id = actor->id,
              .start_time = start_time,
              .end_time = end_time,
              .demand = task.demand,
              .objective_coefficient = option_objective_coefficient(task, *actor, options, start_time, end_time),
          });
          decision.option_indexes.push_back(option_index);
          formulation.horizon_end = std::max(formulation.horizon_end, end_time);
          for (Time time = start_time; time < end_time; ++time) {
            slot_option_indexes[{actor_index, time}].push_back(option_index);
          }
        }
      }
    }

    formulation.task_decisions.push_back(std::move(decision));

    for (const TaskId& dependency_id : task.dependency_task_ids) {
      const auto dependency_it = task_indexes.find(dependency_id);
      if (dependency_it != task_indexes.end()) {
        formulation.dependencies.push_back(
            DependencyConstraint{.predecessor_task_index = dependency_it->second, .successor_task_index = task_index});
      }
    }

    for (const TaskId& mutex_task_id : task.mutually_exclusive_task_ids) {
      const auto mutex_it = task_indexes.find(mutex_task_id);
      if (mutex_it == task_indexes.end()) {
        continue;
      }
      const size_t first = std::min(task_index, mutex_it->second);
      const size_t second = std::max(task_index, mutex_it->second);
      if (first != second && mutex_pairs.insert({first, second}).second) {
        formulation.mutual_exclusions.push_back(
            MutualExclusionConstraint{.first_task_index = first, .second_task_index = second});
      }
    }
  }

  for (const auto& [key, option_indexes] : slot_option_indexes) {
    const size_t actor_index = key.first;
    const Time time = key.second;
    formulation.actor_time_slots.push_back(ActorTimeSlotConstraint{
        .actor_index = actor_index,
        .actor_id = model.actors[actor_index].id,
        .time = time,
        .capacity = model.actors[actor_index].capacity,
        .option_indexes = option_indexes,
    });
  }

  return formulation;
}

OptimizationSolution decode_selected_options(const DiscreteTimeFormulation& formulation,
                                             const std::vector<size_t>& selected_option_indexes,
                                             std::string backend_name,
                                             OptimizationStats stats) {
  OptimizationSolution solution;
  solution.ok = true;
  solution.backend_name = std::move(backend_name);
  solution.stats = stats;

  if (formulation.model == nullptr) {
    solution.ok = false;
    solution.error_message = "No optimization model was attached to the discrete-time formulation.";
    return solution;
  }

  std::unordered_set<TaskId> selected_tasks;
  selected_tasks.reserve(selected_option_indexes.size());
  std::vector<OptimizedAssignment> assignments;
  assignments.reserve(selected_option_indexes.size());
  for (const size_t option_index : selected_option_indexes) {
    if (option_index >= formulation.options.size()) {
      continue;
    }
    const DiscreteOption& option = formulation.options[option_index];
    assignments.push_back(OptimizedAssignment{
        .task_id = option.task_id,
        .actor_id = option.actor_id,
        .start_time = option.start_time,
        .end_time = option.end_time,
    });
    selected_tasks.insert(option.task_id);
  }

  std::ranges::sort(assignments, [](const OptimizedAssignment& lhs, const OptimizedAssignment& rhs) {
    return std::tuple(lhs.start_time, lhs.end_time, lhs.task_id, lhs.actor_id) <
           std::tuple(rhs.start_time, rhs.end_time, rhs.task_id, rhs.actor_id);
  });
  solution.assignments = std::move(assignments);

  solution.unfulfilled_task_ids.reserve(formulation.model->tasks.size() - selected_tasks.size());
  for (const OptimizerTask& task : formulation.model->tasks) {
    if (!selected_tasks.contains(task.id)) {
      solution.unfulfilled_task_ids.push_back(task.id);
    }
  }
  return solution;
}

}  // namespace task_orchestrator::optimizer
