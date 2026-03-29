#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "task_orchestrator/strategy/edf_strategy.hpp"
#include "task_orchestrator/strategy/fifo_strategy.hpp"
#include "task_orchestrator/strategy/priority_only_strategy.hpp"
#include "task_orchestrator/strategy/sjf_strategy.hpp"

namespace to = task_orchestrator;

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

constexpr uint64_t kDefaultSeed = 12345U;
constexpr int kMinimumIterations = 10;
constexpr int kDefaultIterations = kMinimumIterations;
constexpr to::Time kDefaultHorizonEnd = 120;
constexpr int64_t kStrategyIterationSeedStride = 101;
constexpr std::string_view kDefaultMarkdownReportPath =
    "task_orchestrator/benchmark/results/runtime_strategy_latest_report.md";
constexpr std::string_view kDefaultCsvReportPath =
    "task_orchestrator/benchmark/results/runtime_strategy_latest_report.csv";

enum class ScenarioGoal {
  Throughput,
  Deadlines,
  Stability,
};

enum class EventKind {
  ActorUnavailable,
  ActorAvailable,
  TaskFailResumable,
};

struct StrategyBenchmarkConfig {
  uint64_t seed = kDefaultSeed;
  int iterations = kDefaultIterations;
  std::string scenario_filter;
  std::string markdown_output_path;
  std::string csv_output_path;
};

struct ScenarioEvent {
  to::Time time = 0;
  EventKind kind = EventKind::ActorUnavailable;
  to::TaskId task_id;
  to::ActorId actor_id;
};

struct DynamicScenarioInput {
  to::Workflow workflow;
  to::ActorRegistry registry;
  to::WorkflowState state;
  to::ActorRankingProfile ranking_profile;
  std::vector<ScenarioEvent> events;
  to::Time horizon_end = kDefaultHorizonEnd;
};

struct DynamicScenarioDefinition {
  std::string name;
  std::string description;
  ScenarioGoal goal = ScenarioGoal::Throughput;
  bool chaotic = false;
  std::function<DynamicScenarioInput(uint64_t)> build;
};

struct NamedStrategy {
  std::string name;
  const to::SchedulingStrategy* strategy = nullptr;
};

struct DynamicRunMetrics {
  bool ok = true;
  size_t total_tasks = 0;
  size_t completed_tasks = 0;
  size_t deadline_misses = 0;
  size_t dispatched_assignments = 0;
  size_t constraint_violations = 0;
  size_t planning_rounds = 0;
  size_t replan_rounds = 0;
  size_t preferred_actor_hits = 0;
  size_t preferred_actor_opportunities = 0;
  double utilization_ratio = 0.0;
  double mean_assignment_churn = 0.0;
  double total_travel_distance = 0.0;
  double total_execution_cost = 0.0;
  int64_t total_tardiness = 0;
  to::Time makespan = 0;
  std::vector<int64_t> planning_latencies_ns;
};

struct StrategyReportRow {
  std::string scenario_name;
  std::string description;
  std::string strategy_name;
  ScenarioGoal goal = ScenarioGoal::Throughput;
  bool chaotic = false;
  bool ok = true;
  double mean_latency_ns = 0.0;
  double p95_latency_ns = 0.0;
  double completion_ratio = 0.0;
  double deadline_miss_rate = 0.0;
  double constraint_violation_rate = 0.0;
  double mean_tardiness = 0.0;
  double mean_makespan = 0.0;
  double mean_utilization = 0.0;
  double mean_assignment_churn = 0.0;
  double mean_replans = 0.0;
  double preferred_actor_hit_ratio = 0.0;
  double mean_travel_distance = 0.0;
  double mean_execution_cost = 0.0;
};

struct BenchmarkArtifacts {
  std::string markdown_report;
  std::string csv_report;
};

const char* goal_name(const ScenarioGoal goal) {
  switch (goal) {
    case ScenarioGoal::Throughput:
      return "throughput";
    case ScenarioGoal::Deadlines:
      return "deadlines";
    case ScenarioGoal::Stability:
      return "stability";
  }
  return "unknown";
}

bool starts_with(const std::string_view value, const std::string_view prefix) {
  return value.size() >= prefix.size() && value.starts_with(prefix);
}

bool matches_filter(const std::string& value, const std::string& filter) {
  return filter.empty() || value.find(filter) != std::string::npos;
}

std::vector<to::TaskId> all_task_ids(const to::Workflow& workflow) {
  std::vector<to::TaskId> task_ids;
  for (const to::PhaseId& phase_id : workflow.phase_ids()) {
    const std::vector<to::TaskId> phase_task_ids = workflow.task_ids_for_phase(phase_id);
    task_ids.insert(task_ids.end(), phase_task_ids.begin(), phase_task_ids.end());
  }
  return task_ids;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
  return std::ranges::find(values, value) != values.end();
}

void insert_unique(std::vector<std::string>& values, const std::string& value) {
  if (!contains(values, value)) {
    values.push_back(value);
  }
}

void erase_value(std::vector<std::string>& values, const std::string& value) {
  auto [first, last] = std::ranges::remove(values, value);
  values.erase(first, last);
}

to::Duration task_duration(const to::Workflow& workflow, const to::TaskId& task_id) {
  const to::Process* process = workflow.process_for_task(task_id);
  return process ? process->estimated_duration : 1;
}

int task_demand(const to::Workflow& workflow, const to::TaskId& task_id) {
  const to::Process* process = workflow.process_for_task(task_id);
  return process ? process->demand : 1;
}

std::optional<to::Time> task_deadline(const to::Workflow& workflow, const to::TaskId& task_id) {
  const to::Process* process = workflow.process_for_task(task_id);
  return process ? process->deadline : std::nullopt;
}

to::Time task_release_time(const to::Workflow& workflow, const to::WorkflowState& state, const to::TaskId& task_id) {
  if (const auto release_it = state.task_release_time.find(task_id); release_it != state.task_release_time.end()) {
    return release_it->second;
  }
  const to::Process* process = workflow.process_for_task(task_id);
  return process ? process->release_time : 0;
}

std::optional<to::Time> task_latest_start_time(const to::Workflow& workflow, const to::TaskId& task_id) {
  const to::Process* process = workflow.process_for_task(task_id);
  return process ? process->latest_start_time : std::nullopt;
}

to::Time fallback_distance_for_task(const to::Process* process) {
  if (process == nullptr || process->actor_distances.empty()) {
    return 0;
  }
  const auto farthest =
      std::ranges::max_element(process->actor_distances, {}, [](const auto& entry) { return entry.second; });
  return farthest == process->actor_distances.end() ? 0 : farthest->second + 1;
}

to::Time task_distance(const to::Workflow& workflow,
                       const to::WorkflowState& state,
                       const to::TaskId& task_id,
                       const to::ActorId& actor_id) {
  if (const auto override_it = state.task_actor_distance.find(task_id);
      override_it != state.task_actor_distance.end()) {
    if (const auto actor_it = override_it->second.find(actor_id); actor_it != override_it->second.end()) {
      return actor_it->second;
    }
    if (!override_it->second.empty()) {
      const auto farthest =
          std::ranges::max_element(override_it->second, {}, [](const auto& entry) { return entry.second; });
      return farthest == override_it->second.end() ? 0 : farthest->second + 1;
    }
  }
  const to::Process* process = workflow.process_for_task(task_id);
  if (process == nullptr) {
    return 0;
  }
  if (const auto actor_it = process->actor_distances.find(actor_id); actor_it != process->actor_distances.end()) {
    return actor_it->second;
  }
  return fallback_distance_for_task(process);
}

bool actor_matches_task_constraints(const to::Process& process,
                                    const to::WorkflowState& state,
                                    const to::TaskId& task_id,
                                    const to::Actor& actor) {
  if (const auto allowed_it = state.task_allowed_actors.find(task_id);
      allowed_it != state.task_allowed_actors.end() && !allowed_it->second.empty() &&
      std::ranges::find(allowed_it->second, actor.id) == allowed_it->second.end()) {
    return false;
  }
  if (!process.allowed_actor_ids.empty() &&
      std::ranges::find(process.allowed_actor_ids, actor.id) == process.allowed_actor_ids.end()) {
    return false;
  }
  if (!process.allowed_actor_types.empty() &&
      std::ranges::find(process.allowed_actor_types, actor.type) == process.allowed_actor_types.end()) {
    return false;
  }
  return std::ranges::all_of(process.required_capabilities, [&actor](const std::string& capability) {
    return std::ranges::find(actor.capabilities, capability) != actor.capabilities.end();
  });
}

bool preferred_actor_hit(const to::Workflow& workflow,
                         const to::WorkflowState& state,
                         const to::TaskId& task_id,
                         const to::ActorId& actor_id) {
  if (const auto preferred_it = state.task_preferred_actors.find(task_id);
      preferred_it != state.task_preferred_actors.end()) {
    return std::ranges::find(preferred_it->second, actor_id) != preferred_it->second.end();
  }
  const to::Process* process = workflow.process_for_task(task_id);
  return process != nullptr &&
         std::ranges::find(process->preferred_actor_ids, actor_id) != process->preferred_actor_ids.end();
}

bool has_preferred_actor_targets(const to::Workflow& workflow,
                                 const to::WorkflowState& state,
                                 const to::TaskId& task_id) {
  if (const auto preferred_it = state.task_preferred_actors.find(task_id);
      preferred_it != state.task_preferred_actors.end()) {
    return !preferred_it->second.empty();
  }
  const to::Process* process = workflow.process_for_task(task_id);
  return process != nullptr && !process->preferred_actor_ids.empty();
}

bool dependencies_completed_for_dispatch(const to::Process& process,
                                         const to::WorkflowState& state,
                                         const to::Time assignment_start_time) {
  return std::ranges::all_of(process.dependency_task_ids, [&](const to::TaskId& dependency_id) {
    if (!contains(state.completed_tasks, dependency_id)) {
      return false;
    }
    const auto completion_it = state.task_actual_completion_time.find(dependency_id);
    return completion_it != state.task_actual_completion_time.end() && completion_it->second <= assignment_start_time;
  });
}

bool mutex_clear_for_dispatch(const to::Process& process, const to::WorkflowState& state) {
  return std::ranges::none_of(process.mutually_exclusive_task_ids, [&](const to::TaskId& task_id) {
    return contains(state.completed_tasks, task_id) || contains(state.assigned_tasks, task_id);
  });
}

bool assignment_is_constraint_compliant(const to::Workflow& workflow,
                                        const to::ActorRegistry& registry,
                                        const to::WorkflowState& state,
                                        const to::Assignment& assignment) {
  const to::Process* process = workflow.process_for_task(assignment.task_id);
  const to::Actor* actor = registry.get(assignment.actor_id);
  if (process == nullptr || actor == nullptr) {
    return false;
  }
  if (!actor_matches_task_constraints(*process, state, assignment.task_id, *actor)) {
    return false;
  }
  if (task_release_time(workflow, state, assignment.task_id) > assignment.start_time) {
    return false;
  }
  if (const std::optional<to::Time> latest_start = task_latest_start_time(workflow, assignment.task_id);
      latest_start && assignment.start_time > *latest_start) {
    return false;
  }
  if (const std::optional<to::Time> deadline = task_deadline(workflow, assignment.task_id);
      deadline && assignment.start_time + task_duration(workflow, assignment.task_id) > *deadline) {
    return false;
  }
  if (!dependencies_completed_for_dispatch(*process, state, assignment.start_time)) {
    return false;
  }
  if (!mutex_clear_for_dispatch(*process, state)) {
    return false;
  }
  int current_load = 0;
  if (state.actor_load.contains(assignment.actor_id)) {
    current_load = state.actor_load.at(assignment.actor_id);
  } else if (actor != nullptr) {
    current_load = actor->current_load;
  }
  return current_load + task_demand(workflow, assignment.task_id) <= actor->capacity;
}

void update_phase_completion(const to::Workflow& workflow, to::WorkflowState& state) {
  for (const to::PhaseId& phase_id : workflow.phase_ids()) {
    if (contains(state.completed_phases, phase_id)) {
      continue;
    }
    const std::vector<to::TaskId> phase_task_ids = workflow.task_ids_for_phase(phase_id);
    if (!phase_task_ids.empty() && std::ranges::all_of(phase_task_ids, [&state](const to::TaskId& task_id) {
          return contains(state.completed_tasks, task_id);
        })) {
      state.completed_phases.push_back(phase_id);
    }
  }
}

double percentile(std::vector<int64_t> values, const double percentile_rank) {
  if (values.empty()) {
    return 0.0;
  }
  std::ranges::sort(values);
  const double bounded_rank = std::clamp(percentile_rank, 0.0, 1.0);
  const auto index = static_cast<size_t>(bounded_rank * static_cast<double>(values.size() - 1));
  return static_cast<double>(values[index]);
}

template <typename Range, typename Projection>
double arithmetic_mean(const Range& values, Projection projection) {
  if (values.empty()) {
    return 0.0;
  }
  const double sum = std::accumulate(values.begin(), values.end(), 0.0, [&](const double running, const auto& value) {
    return running + projection(value);
  });
  return sum / static_cast<double>(values.size());
}

double assignment_churn_ratio(const std::unordered_map<to::TaskId, to::Assignment>& previous_plan,
                              const std::unordered_map<to::TaskId, to::Assignment>& current_plan) {
  std::set<to::TaskId> task_ids;
  for (const auto& [task_id, _] : previous_plan) {
    task_ids.insert(task_id);
  }
  for (const auto& [task_id, _] : current_plan) {
    task_ids.insert(task_id);
  }
  if (task_ids.empty()) {
    return 0.0;
  }
  size_t changed_tasks = 0;
  for (const to::TaskId& task_id : task_ids) {
    const auto previous_it = previous_plan.find(task_id);
    const auto current_it = current_plan.find(task_id);
    if (previous_it == previous_plan.end() || current_it == current_plan.end()) {
      ++changed_tasks;
      continue;
    }
    if (previous_it->second.actor_id != current_it->second.actor_id ||
        previous_it->second.start_time != current_it->second.start_time) {
      ++changed_tasks;
    }
  }
  return static_cast<double>(changed_tasks) / static_cast<double>(task_ids.size());
}

std::unordered_map<to::TaskId, to::Assignment> future_plan_from_schedule(const to::ScheduleResult& schedule,
                                                                         const to::WorkflowState& state,
                                                                         const to::Time current_time) {
  std::unordered_map<to::TaskId, to::Assignment> plan;
  for (const to::Assignment& assignment : schedule.assignments) {
    if (assignment.start_time < current_time) {
      continue;
    }
    if (contains(state.completed_tasks, assignment.task_id) || contains(state.assigned_tasks, assignment.task_id)) {
      continue;
    }
    plan[assignment.task_id] = assignment;
  }
  return plan;
}

void mark_task_completed(const to::Workflow& workflow,
                         to::ActorRegistry& registry,
                         to::WorkflowState& state,
                         const to::TaskId& task_id,
                         const to::ActorId& actor_id,
                         const to::Time completion_time) {
  erase_value(state.assigned_tasks, task_id);
  insert_unique(state.completed_tasks, task_id);
  erase_value(state.resumable_tasks, task_id);
  erase_value(state.unresumable_tasks, task_id);
  if (to::Actor* actor = registry.get_mutable(actor_id); actor != nullptr && actor->current_load > 0) {
    actor->current_load = std::max(0, actor->current_load - task_demand(workflow, task_id));
    state.actor_load[actor_id] = actor->current_load;
  }
  state.task_actor.erase(task_id);
  state.task_actual_completion_time[task_id] = completion_time;
  update_phase_completion(workflow, state);
}

void mark_task_failed(to::ActorRegistry& registry,
                      const to::Workflow& workflow,
                      to::WorkflowState& state,
                      const to::TaskId& task_id,
                      const to::ActorId& actor_id) {
  erase_value(state.assigned_tasks, task_id);
  erase_value(state.completed_tasks, task_id);
  if (to::Actor* actor = registry.get_mutable(actor_id); actor != nullptr && actor->current_load > 0) {
    actor->current_load = std::max(0, actor->current_load - task_demand(workflow, task_id));
    state.actor_load[actor_id] = actor->current_load;
  }
  state.task_actor.erase(task_id);
  erase_value(state.unresumable_tasks, task_id);
  erase_value(state.resumable_tasks, task_id);
  insert_unique(state.resumable_tasks, task_id);
}

void apply_event(const ScenarioEvent& event,
                 const to::Workflow& workflow,
                 to::ActorRegistry& registry,
                 to::WorkflowState& state) {
  switch (event.kind) {
    case EventKind::ActorUnavailable:
      insert_unique(state.unavailable_actors, event.actor_id);
      break;
    case EventKind::ActorAvailable:
      erase_value(state.unavailable_actors, event.actor_id);
      break;
    case EventKind::TaskFailResumable:
      mark_task_failed(registry, workflow, state, event.task_id, event.actor_id);
      break;
  }
}

struct CompletionEvent {
  to::Time time = 0;
  to::TaskId task_id;
  to::ActorId actor_id;
};

DynamicRunMetrics run_dynamic_strategy_scenario(const DynamicScenarioInput& scenario,
                                                const NamedStrategy& named_strategy) {
  DynamicRunMetrics metrics;
  metrics.total_tasks = all_task_ids(scenario.workflow).size();

  to::WorkflowState state = scenario.state;
  to::ActorRegistry registry = scenario.registry;
  std::vector<ScenarioEvent> events = scenario.events;
  std::ranges::sort(events, {}, &ScenarioEvent::time);

  std::vector<CompletionEvent> completion_events;
  std::unordered_map<to::TaskId, to::Assignment> planned_assignments;
  std::unordered_map<to::TaskId, to::Assignment> previous_plan;
  std::vector<double> churn_samples;
  const std::vector<to::ActorId> actor_ids = registry.actor_ids();

  size_t event_index = 0;
  bool needs_planning = true;
  to::Time current_time = 0;
  double busy_capacity_time = 0.0;
  double total_capacity_time = 0.0;

  while (current_time <= scenario.horizon_end && state.completed_tasks.size() < metrics.total_tasks) {
    bool state_changed = false;

    for (const CompletionEvent& completion : completion_events) {
      if (completion.time != current_time) {
        continue;
      }
      const auto owner_it = state.task_actor.find(completion.task_id);
      if (owner_it != state.task_actor.end() && owner_it->second == completion.actor_id) {
        mark_task_completed(scenario.workflow, registry, state, completion.task_id, completion.actor_id, current_time);
        state_changed = true;
      }
    }

    while (event_index < events.size() && events[event_index].time == current_time) {
      apply_event(events[event_index], scenario.workflow, registry, state);
      state_changed = true;
      ++event_index;
    }

    if (needs_planning || state_changed) {
      const auto planning_start = Clock::now();
      const to::ScheduleResult schedule = to::Scheduler::plan(
          scenario.workflow, state, registry, current_time, named_strategy.strategy, &scenario.ranking_profile);
      const auto planning_end = Clock::now();
      metrics.planning_latencies_ns.push_back(
          std::chrono::duration_cast<Nanoseconds>(planning_end - planning_start).count());
      metrics.ok = metrics.ok && schedule.ok;
      ++metrics.planning_rounds;
      if (metrics.planning_rounds > 1) {
        ++metrics.replan_rounds;
      }
      planned_assignments = future_plan_from_schedule(schedule, state, current_time);
      if (!previous_plan.empty()) {
        churn_samples.push_back(assignment_churn_ratio(previous_plan, planned_assignments));
      }
      previous_plan = planned_assignments;
      needs_planning = false;
    }

    for (auto assignment_it = planned_assignments.begin(); assignment_it != planned_assignments.end();) {
      if (assignment_it->second.start_time > current_time) {
        ++assignment_it;
        continue;
      }
      const to::Assignment& assignment = assignment_it->second;
      if (contains(state.completed_tasks, assignment.task_id) || contains(state.assigned_tasks, assignment.task_id)) {
        assignment_it = planned_assignments.erase(assignment_it);
        continue;
      }
      ++metrics.dispatched_assignments;
      if (!assignment_is_constraint_compliant(scenario.workflow, registry, state, assignment)) {
        ++metrics.constraint_violations;
      }
      if (has_preferred_actor_targets(scenario.workflow, state, assignment.task_id)) {
        ++metrics.preferred_actor_opportunities;
        if (preferred_actor_hit(scenario.workflow, state, assignment.task_id, assignment.actor_id)) {
          ++metrics.preferred_actor_hits;
        }
      }
      metrics.total_travel_distance +=
          static_cast<double>(task_distance(scenario.workflow, state, assignment.task_id, assignment.actor_id));
      if (const to::Actor* actor = registry.get(assignment.actor_id); actor != nullptr) {
        metrics.total_execution_cost +=
            actor->execution_cost_per_unit * static_cast<double>(task_duration(scenario.workflow, assignment.task_id));
      }
      insert_unique(state.assigned_tasks, assignment.task_id);
      erase_value(state.resumable_tasks, assignment.task_id);
      state.task_actor[assignment.task_id] = assignment.actor_id;
      state.task_planned_start_time[assignment.task_id] = assignment.start_time;
      state.task_planned_end_time[assignment.task_id] =
          assignment.start_time + task_duration(scenario.workflow, assignment.task_id);
      if (to::Actor* actor = registry.get_mutable(assignment.actor_id); actor != nullptr) {
        actor->current_load += task_demand(scenario.workflow, assignment.task_id);
        state.actor_load[assignment.actor_id] = actor->current_load;
      }
      completion_events.push_back(CompletionEvent{.time = state.task_planned_end_time[assignment.task_id],
                                                  .task_id = assignment.task_id,
                                                  .actor_id = assignment.actor_id});
      assignment_it = planned_assignments.erase(assignment_it);
    }

    if (state.completed_tasks.size() >= metrics.total_tasks) {
      break;
    }

    std::optional<to::Time> next_time;
    auto consider_next_time = [&next_time, current_time](const to::Time candidate_time) {
      if (candidate_time <= current_time) {
        return;
      }
      if (!next_time || candidate_time < *next_time) {
        next_time = candidate_time;
      }
    };

    for (size_t pending_event_index = event_index; pending_event_index < events.size(); ++pending_event_index) {
      consider_next_time(events[pending_event_index].time);
    }
    for (const CompletionEvent& completion : completion_events) {
      consider_next_time(completion.time);
    }
    for (const auto& [_, assignment] : planned_assignments) {
      consider_next_time(assignment.start_time);
    }

    if (!next_time) {
      break;
    }

    const to::Time delta = *next_time - current_time;
    for (const to::ActorId& actor_id : actor_ids) {
      const to::Actor* actor = registry.get(actor_id);
      if (actor == nullptr) {
        continue;
      }
      busy_capacity_time += static_cast<double>(actor->current_load * delta);
      total_capacity_time += static_cast<double>(actor->capacity * delta);
    }
    current_time = *next_time;
  }

  metrics.completed_tasks = state.completed_tasks.size();
  metrics.mean_assignment_churn = churn_samples.empty()
                                      ? 0.0
                                      : std::accumulate(churn_samples.begin(), churn_samples.end(), 0.0) /
                                            static_cast<double>(churn_samples.size());
  metrics.utilization_ratio = total_capacity_time > 0.0 ? (busy_capacity_time / total_capacity_time) : 0.0;

  for (const to::TaskId& task_id : state.completed_tasks) {
    if (const std::optional<to::Time> deadline = task_deadline(scenario.workflow, task_id);
        deadline && state.task_actual_completion_time.contains(task_id) &&
        state.task_actual_completion_time.at(task_id) > *deadline) {
      ++metrics.deadline_misses;
      metrics.total_tardiness += state.task_actual_completion_time.at(task_id) - *deadline;
    }
  }
  for (const auto& [_, completion_time] : state.task_actual_completion_time) {
    metrics.makespan = std::max(metrics.makespan, completion_time);
  }
  return metrics;
}

to::Workflow make_workflow(const std::string& workflow_id,
                           const std::vector<to::Phase>& phases,
                           const std::vector<to::Process>& processes) {
  to::Workflow workflow(workflow_id);
  for (const to::Phase& phase : phases) {
    workflow.add_phase(phase);
  }
  for (const to::Process& process : processes) {
    workflow.add_process(process);
  }
  return workflow;
}

to::ActorRegistry make_registry(const std::vector<std::tuple<to::ActorId, int, to::Time, to::Time>>& actor_specs) {
  to::ActorRegistry registry;
  for (const auto& [actor_id, capacity, start, end] : actor_specs) {
    registry.add(to::Actor{
        .id = actor_id,
        .capacity = capacity,
        .availability_windows = {to::AvailabilityWindow{.start = start, .end = end}},
        .current_load = 0,
    });
  }
  return registry;
}

to::ActorRegistry make_registry(const std::vector<to::Actor>& actors) {
  to::ActorRegistry registry;
  for (const to::Actor& actor : actors) {
    registry.add(actor);
  }
  return registry;
}

std::vector<DynamicScenarioDefinition> dynamic_scenarios() {
  struct ThroughputVariant {
    std::string suffix;
    int task_count = 0;
    to::Time second_wave_time = 0;
    bool chaotic = false;
  };
  struct DeadlineVariant {
    std::string suffix;
    to::Time outage_start = 0;
    to::Time outage_end = 0;
    int task_count = 0;
  };
  struct RecoveryVariant {
    std::string suffix;
    std::vector<ScenarioEvent> events;
  };
  struct DependencyVariant {
    std::string suffix;
    to::Time delayed_release = 0;
    std::vector<ScenarioEvent> events;
  };
  struct CapabilityVariant {
    std::string suffix;
    bool severe = false;
  };

  static const std::array<ThroughputVariant, 2> kThroughputVariants = {{
      {.suffix = "balanced_release_waves", .task_count = 8, .second_wave_time = 6, .chaotic = false},
      {.suffix = "chaotic_release_storm", .task_count = 12, .second_wave_time = 4, .chaotic = true},
  }};
  static const std::array<DeadlineVariant, 2> kDeadlineVariants = {{
      {.suffix = "moderate_gap", .outage_start = 5, .outage_end = 14, .task_count = 6},
      {.suffix = "severe_flapping_gap", .outage_start = 3, .outage_end = 16, .task_count = 8},
  }};
  static const std::array<RecoveryVariant, 2> kRecoveryVariants = {{
      {.suffix = "dual_failure_wave",
       .events =
           {
               {.time = 4, .kind = EventKind::TaskFailResumable, .task_id = "recover_0", .actor_id = "A1"},
               {.time = 4, .kind = EventKind::ActorUnavailable, .task_id = {}, .actor_id = "A1"},
               {.time = 7, .kind = EventKind::TaskFailResumable, .task_id = "recover_1", .actor_id = "A2"},
               {.time = 12, .kind = EventKind::ActorAvailable, .task_id = {}, .actor_id = "A1"},
           }},
      {.suffix = "cascading_failure_wave",
       .events =
           {
               {.time = 3, .kind = EventKind::TaskFailResumable, .task_id = "recover_0", .actor_id = "A1"},
               {.time = 4, .kind = EventKind::TaskFailResumable, .task_id = "recover_1", .actor_id = "A2"},
               {.time = 5, .kind = EventKind::ActorUnavailable, .task_id = {}, .actor_id = "A2"},
               {.time = 7, .kind = EventKind::TaskFailResumable, .task_id = "recover_2", .actor_id = "A3"},
               {.time = 10, .kind = EventKind::ActorUnavailable, .task_id = {}, .actor_id = "A1"},
               {.time = 14, .kind = EventKind::ActorAvailable, .task_id = {}, .actor_id = "A2"},
               {.time = 18, .kind = EventKind::ActorAvailable, .task_id = {}, .actor_id = "A1"},
           }},
  }};
  static const std::array<DependencyVariant, 2> kDependencyVariants = {{
      {.suffix = "temporary_capacity_loss",
       .delayed_release = 10,
       .events =
           {
               {.time = 6, .kind = EventKind::ActorUnavailable, .task_id = {}, .actor_id = "A2"},
               {.time = 11, .kind = EventKind::ActorAvailable, .task_id = {}, .actor_id = "A2"},
           }},
      {.suffix = "cascading_multiphase_outage",
       .delayed_release = 8,
       .events =
           {
               {.time = 4, .kind = EventKind::ActorUnavailable, .task_id = {}, .actor_id = "A2"},
               {.time = 8, .kind = EventKind::ActorUnavailable, .task_id = {}, .actor_id = "A1"},
               {.time = 12, .kind = EventKind::ActorAvailable, .task_id = {}, .actor_id = "A2"},
               {.time = 16, .kind = EventKind::ActorAvailable, .task_id = {}, .actor_id = "A1"},
           }},
  }};
  static const std::array<CapabilityVariant, 2> kCapabilityVariants = {{
      {.suffix = "capability_fragmentation_shift_gap", .severe = false},
      {.suffix = "severe_fragmentation_shift_gap", .severe = true},
  }};

  std::vector<DynamicScenarioDefinition> scenarios;
  scenarios.reserve(kThroughputVariants.size() + kDeadlineVariants.size() + kRecoveryVariants.size() +
                    kDependencyVariants.size() + kCapabilityVariants.size());

  for (const ThroughputVariant& variant : kThroughputVariants) {
    scenarios.push_back(DynamicScenarioDefinition{
        .name = "throughput_release_contention_responsiveness_" + variant.suffix,
        .description = "Verifies how quickly each strategy reacts to rolling releases while preserving throughput and "
                       "makespan quality under contention.",
        .goal = ScenarioGoal::Throughput,
        .chaotic = variant.chaotic,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              std::vector<to::Process> processes;
              std::vector<std::string> process_ids;
              to::WorkflowState state;
              for (int task_index = 0; task_index < variant.task_count; ++task_index) {
                const std::string task_id = "burst_" + std::to_string(task_index);
                process_ids.push_back(task_id);
                const to::Duration duration = 2 + static_cast<to::Duration>(rng() % 4);
                const bool late_wave = task_index >= variant.task_count / 2;
                const to::Time release_time =
                    late_wave ? variant.second_wave_time + static_cast<to::Time>(rng() % (variant.chaotic ? 5 : 3))
                              : static_cast<to::Time>(rng() % (variant.chaotic ? 3 : 1));
                processes.push_back(to::Process{
                    .id = task_id,
                    .phase_id = "phase",
                    .sub_process_ids = {},
                    .estimated_duration = duration,
                    .priority = static_cast<to::Priority>(15 - (task_index % 7)),
                    .deadline = 28 + duration + release_time + static_cast<to::Time>(task_index / 2),
                });
                state.task_release_time[task_id] = release_time;
              }
              return DynamicScenarioInput{
                  .workflow = make_workflow(
                      "throughput_release_contention_responsiveness_" + variant.suffix,
                      {to::Phase{
                          .id = "phase", .name = "Burst", .process_ids = process_ids, .dependency_phase_ids = {}}},
                      processes),
                  .registry = make_registry(
                      variant.chaotic
                          ? std::vector<std::tuple<to::ActorId, int, to::Time, to::Time>>{{"A1", 1, 0, 80},
                                                                                          {"A2", 1, 0, 80},
                                                                                          {"A3", 1, 4, 60}}
                          : std::vector<std::tuple<to::ActorId, int, to::Time, to::Time>>{{"A1", 1, 0, 80},
                                                                                          {"A2", 1, 0, 80}}),
                  .state = std::move(state),
                  .ranking_profile = {.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                   to::ActorRankingCriterion::LeastLoaded}},
                  .events = variant.chaotic ? std::vector<ScenarioEvent>{{.time = 9,
                                                                          .kind = EventKind::ActorUnavailable,
                                                                          .task_id = {},
                                                                          .actor_id = "A2"},
                                                                         {.time = 13,
                                                                          .kind = EventKind::ActorAvailable,
                                                                          .task_id = {},
                                                                          .actor_id = "A2"}}
                                            : std::vector<ScenarioEvent>{},
                  .horizon_end = 90,
              };
            },
    });
  }

  for (const DeadlineVariant& variant : kDeadlineVariants) {
    scenarios.push_back(DynamicScenarioDefinition{
        .name = "deadline_resilience_with_actor_flapping_" + variant.suffix,
        .description = "Verifies deadline protection when capacity flaps during a deadline-heavy burst.",
        .goal = ScenarioGoal::Deadlines,
        .chaotic = true,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              std::vector<to::Process> processes;
              std::vector<std::string> process_ids;
              for (int task_index = 0; task_index < variant.task_count; ++task_index) {
                const std::string task_id = "rush_" + std::to_string(task_index);
                process_ids.push_back(task_id);
                const to::Duration duration = 3 + static_cast<to::Duration>(rng() % 5);
                processes.push_back(to::Process{
                    .id = task_id,
                    .phase_id = "phase",
                    .sub_process_ids = {},
                    .estimated_duration = duration,
                    .priority = static_cast<to::Priority>(25 - task_index),
                    .deadline = 10 + static_cast<to::Time>(task_index * 2) + duration,
                });
              }
              return DynamicScenarioInput{
                  .workflow = make_workflow("deadline_resilience_with_actor_flapping_" + variant.suffix,
                                            {to::Phase{.id = "phase",
                                                       .name = "DeadlineCrunch",
                                                       .process_ids = process_ids,
                                                       .dependency_phase_ids = {}}},
                                            processes),
                  .registry = make_registry({{"A1", 1, 0, 90}, {"A2", 1, 0, 90}}),
                  .state = to::WorkflowState{},
                  .ranking_profile = {.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                   to::ActorRankingCriterion::PreferredActor}},
                  .events =
                      {
                          {.time = variant.outage_start,
                           .kind = EventKind::ActorUnavailable,
                           .task_id = {},
                           .actor_id = "A2"},
                          {.time = variant.outage_end,
                           .kind = EventKind::ActorAvailable,
                           .task_id = {},
                           .actor_id = "A2"},
                      },
                  .horizon_end = 90,
              };
            },
    });
  }

  for (const RecoveryVariant& variant : kRecoveryVariants) {
    scenarios.push_back(DynamicScenarioDefinition{
        .name = "replanning_stability_under_resumable_failures_" + variant.suffix,
        .description = "Verifies replanning stability and churn when assigned work repeatedly fails and re-enters the "
                       "ready queue.",
        .goal = ScenarioGoal::Stability,
        .chaotic = true,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              std::vector<to::Process> processes;
              std::vector<std::string> process_ids;
              for (int task_index = 0; task_index < 6; ++task_index) {
                const std::string task_id = "recover_" + std::to_string(task_index);
                process_ids.push_back(task_id);
                processes.push_back(to::Process{
                    .id = task_id,
                    .phase_id = "phase",
                    .sub_process_ids = {},
                    .estimated_duration = 4 + static_cast<to::Duration>(rng() % 3),
                    .priority = static_cast<to::Priority>(18 - task_index),
                    .deadline = 34 + static_cast<to::Time>(task_index * 3),
                });
              }
              to::WorkflowState state;
              state.task_preferred_actors["recover_0"] = {"A1"};
              state.task_preferred_actors["recover_1"] = {"A2"};
              state.task_preferred_actors["recover_2"] = {"A3"};
              return DynamicScenarioInput{
                  .workflow = make_workflow(
                      "replanning_stability_under_resumable_failures_" + variant.suffix,
                      {to::Phase{
                          .id = "phase", .name = "Recovery", .process_ids = process_ids, .dependency_phase_ids = {}}},
                      processes),
                  .registry = make_registry({{"A1", 1, 0, 90}, {"A2", 1, 0, 90}, {"A3", 1, 0, 90}}),
                  .state = std::move(state),
                  .ranking_profile = {.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                   to::ActorRankingCriterion::LeastLoaded,
                                                   to::ActorRankingCriterion::PreferredActor}},
                  .events = variant.events,
                  .horizon_end = 100,
              };
            },
    });
  }

  for (const DependencyVariant& variant : kDependencyVariants) {
    scenarios.push_back(DynamicScenarioDefinition{
        .name = "dependency_flow_stability_under_multiphase_disruption_" + variant.suffix,
        .description = "Verifies whether strategy choices remain stable when dependency-gated work and temporary "
                       "capacity loss interact across phases.",
        .goal = ScenarioGoal::Stability,
        .chaotic = true,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              std::vector<to::Process> processes = {
                  {.id = "prep_a",
                   .phase_id = "prep",
                   .sub_process_ids = {},
                   .estimated_duration = 3,
                   .priority = 10,
                   .deadline = 18},
                  {.id = "prep_b",
                   .phase_id = "prep",
                   .sub_process_ids = {},
                   .estimated_duration = 4,
                   .priority = 9,
                   .deadline = 20},
                  {.id = "exec_a",
                   .phase_id = "exec",
                   .sub_process_ids = {},
                   .estimated_duration = 5,
                   .priority = 8,
                   .deadline = 35},
                  {.id = "exec_b",
                   .phase_id = "exec",
                   .sub_process_ids = {},
                   .estimated_duration = 4 + static_cast<to::Duration>(rng() % 3),
                   .priority = 7,
                   .deadline = 36},
                  {.id = "verify",
                   .phase_id = "verify",
                   .sub_process_ids = {},
                   .estimated_duration = 3 + static_cast<to::Duration>(rng() % 2),
                   .priority = 6,
                   .deadline = 48},
              };
              to::WorkflowState state;
              state.task_release_time["exec_b"] = variant.delayed_release;
              state.task_release_time["verify"] = variant.delayed_release + 6;
              return DynamicScenarioInput{
                  .workflow = make_workflow("dependency_flow_stability_under_multiphase_disruption_" + variant.suffix,
                                            {
                                                to::Phase{.id = "prep",
                                                          .name = "Prep",
                                                          .process_ids = {"prep_a", "prep_b"},
                                                          .dependency_phase_ids = {}},
                                                to::Phase{.id = "exec",
                                                          .name = "Exec",
                                                          .process_ids = {"exec_a", "exec_b"},
                                                          .dependency_phase_ids = {"prep"}},
                                                to::Phase{.id = "verify",
                                                          .name = "Verify",
                                                          .process_ids = {"verify"},
                                                          .dependency_phase_ids = {"exec"}},
                                            },
                                            processes),
                  .registry = make_registry({{"A1", 1, 0, 100}, {"A2", 1, 0, 100}}),
                  .state = std::move(state),
                  .ranking_profile = {.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                   to::ActorRankingCriterion::LeastLoaded}},
                  .events = variant.events,
                  .horizon_end = 100,
              };
            },
    });
  }

  for (const CapabilityVariant& variant : kCapabilityVariants) {
    scenarios.push_back(DynamicScenarioDefinition{
        .name = "capability_fragmentation_shift_gap_resilience_" + variant.suffix,
        .description =
            "Verifies plan stability when capabilities are split across actors with staggered availability windows.",
        .goal = ScenarioGoal::Stability,
        .chaotic = true,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              std::vector<to::Process> processes = {
                  {.id = "scan_a",
                   .phase_id = "scan",
                   .sub_process_ids = {},
                   .estimated_duration = 3,
                   .priority = 10,
                   .deadline = 18,
                   .allowed_actor_types = {"robot"},
                   .preferred_actor_ids = {"A1"},
                   .required_capabilities = {"scan"},
                   .actor_distances = {{"A1", 1}, {"A2", 3}, {"A3", 8}}},
                  {.id = "scan_b",
                   .phase_id = "scan",
                   .sub_process_ids = {},
                   .estimated_duration = 4,
                   .priority = 9,
                   .deadline = 20,
                   .allowed_actor_types = {"robot"},
                   .preferred_actor_ids = {"A2"},
                   .required_capabilities = {"scan"},
                   .actor_distances = {{"A1", 4}, {"A2", 1}, {"A3", 7}}},
                  {.id = "lift_a",
                   .phase_id = "lift",
                   .sub_process_ids = {},
                   .estimated_duration = 5,
                   .priority = 8,
                   .deadline = 30,
                   .demand = variant.severe ? 2 : 1,
                   .allowed_actor_ids = {"A2", "A3"},
                   .preferred_actor_ids = {"A2"},
                   .required_capabilities = {"lift"},
                   .actor_distances = {{"A2", 2}, {"A3", 5}}},
                  {.id = "lift_b",
                   .phase_id = "lift",
                   .sub_process_ids = {},
                   .estimated_duration = 4 + static_cast<to::Duration>(rng() % 2),
                   .priority = 7,
                   .deadline = 34,
                   .allowed_actor_ids = {"A2", "A3"},
                   .preferred_actor_ids = {"A3"},
                   .required_capabilities = {"lift"},
                   .actor_distances = {{"A2", 4}, {"A3", 1}}},
              };
              to::WorkflowState state;
              state.task_release_time["lift_b"] = variant.severe ? 12 : 8;
              state.task_actor_distance["lift_b"] =
                  variant.severe ? std::unordered_map<to::ActorId, to::Time>{{"A2", 5}, {"A3", 1}}
                                 : std::unordered_map<to::ActorId, to::Time>{{"A2", 3}, {"A3", 1}};
              return DynamicScenarioInput{
                  .workflow = make_workflow("capability_fragmentation_shift_gap_resilience_" + variant.suffix,
                                            {
                                                to::Phase{.id = "scan",
                                                          .name = "Scan",
                                                          .process_ids = {"scan_a", "scan_b"},
                                                          .dependency_phase_ids = {}},
                                                to::Phase{.id = "lift",
                                                          .name = "Lift",
                                                          .process_ids = {"lift_a", "lift_b"},
                                                          .dependency_phase_ids = {"scan"}},
                                            },
                                            processes),
                  .registry = make_registry(
                      variant.severe
                          ? std::vector<to::Actor>{to::Actor{.id = "A1",
                                                             .type = "robot",
                                                             .capacity = 1,
                                                             .availability_windows = {{.start = 0, .end = 10}},
                                                             .capabilities = {"scan"},
                                                             .execution_cost_per_unit = 0.8,
                                                             .current_load = 0},
                                                   to::Actor{.id = "A2",
                                                             .type = "robot",
                                                             .capacity = 2,
                                                             .availability_windows = {{.start = 8, .end = 18}},
                                                             .capabilities = {"scan", "lift"},
                                                             .execution_cost_per_unit = 1.2,
                                                             .current_load = 0},
                                                   to::Actor{.id = "A3",
                                                             .type = "robot",
                                                             .capacity = 2,
                                                             .availability_windows = {{.start = 16, .end = 40}},
                                                             .capabilities = {"lift"},
                                                             .execution_cost_per_unit = 1.6,
                                                             .current_load = 0}}
                          : std::vector<to::Actor>{to::Actor{.id = "A1",
                                                             .type = "robot",
                                                             .capacity = 1,
                                                             .availability_windows = {{.start = 0, .end = 14}},
                                                             .capabilities = {"scan"},
                                                             .execution_cost_per_unit = 0.8,
                                                             .current_load = 0},
                                                   to::Actor{.id = "A2",
                                                             .type = "robot",
                                                             .capacity = 2,
                                                             .availability_windows = {{.start = 6, .end = 24}},
                                                             .capabilities = {"scan", "lift"},
                                                             .execution_cost_per_unit = 1.1,
                                                             .current_load = 0},
                                                   to::Actor{.id = "A3",
                                                             .type = "robot",
                                                             .capacity = 2,
                                                             .availability_windows = {{.start = 16, .end = 40}},
                                                             .capabilities = {"lift"},
                                                             .execution_cost_per_unit = 1.4,
                                                             .current_load = 0}}),
                  .state = std::move(state),
                  .ranking_profile = {.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                   to::ActorRankingCriterion::PreferredActor,
                                                   to::ActorRankingCriterion::ExecutionCost,
                                                   to::ActorRankingCriterion::LeastLoaded}},
                  .events = variant.severe ? std::vector<ScenarioEvent>{{.time = 9,
                                                                         .kind = EventKind::ActorUnavailable,
                                                                         .task_id = {},
                                                                         .actor_id = "A2"},
                                                                        {.time = 15,
                                                                         .kind = EventKind::ActorAvailable,
                                                                         .task_id = {},
                                                                         .actor_id = "A2"}}
                                           : std::vector<ScenarioEvent>{},
                  .horizon_end = 100,
              };
            },
    });
  }

  return scenarios;
}

std::vector<StrategyReportRow> benchmark_strategies(const StrategyBenchmarkConfig& config) {
  const to::EDFStrategy edf;
  const to::FIFOStrategy fifo;
  const to::SJFStrategy sjf;
  const to::PriorityOnlyStrategy priority_only;
  const std::array<NamedStrategy, 4> strategies = {
      NamedStrategy{.name = "EDF", .strategy = &edf},
      NamedStrategy{.name = "FIFO", .strategy = &fifo},
      NamedStrategy{.name = "SJF", .strategy = &sjf},
      NamedStrategy{.name = "PriorityOnly", .strategy = &priority_only},
  };

  std::vector<StrategyReportRow> rows;
  const auto scenarios = dynamic_scenarios();
  for (const DynamicScenarioDefinition& scenario : scenarios) {
    if (!matches_filter(scenario.name, config.scenario_filter)) {
      continue;
    }
    for (const NamedStrategy& named_strategy : strategies) {
      std::vector<DynamicRunMetrics> runs;
      runs.reserve(config.iterations);
      std::vector<int64_t> planning_latencies;
      for (int iteration = 0; iteration < config.iterations; ++iteration) {
        const DynamicScenarioInput scenario_input =
            scenario.build(config.seed + static_cast<uint64_t>(iteration * kStrategyIterationSeedStride));
        DynamicRunMetrics metrics = run_dynamic_strategy_scenario(scenario_input, named_strategy);
        planning_latencies.insert(
            planning_latencies.end(), metrics.planning_latencies_ns.begin(), metrics.planning_latencies_ns.end());
        runs.push_back(std::move(metrics));
      }

      rows.push_back(StrategyReportRow{
          .scenario_name = scenario.name,
          .description = scenario.description,
          .strategy_name = named_strategy.name,
          .goal = scenario.goal,
          .chaotic = scenario.chaotic,
          .ok = std::ranges::all_of(runs, [](const DynamicRunMetrics& metrics) { return metrics.ok; }),
          .mean_latency_ns = arithmetic_mean(planning_latencies,
                                             [](const int64_t latency_ns) { return static_cast<double>(latency_ns); }),
          .p95_latency_ns = percentile(planning_latencies, 0.95),
          .completion_ratio = arithmetic_mean(runs,
                                              [](const DynamicRunMetrics& metrics) {
                                                return metrics.total_tasks == 0
                                                           ? 0.0
                                                           : static_cast<double>(metrics.completed_tasks) /
                                                                 static_cast<double>(metrics.total_tasks);
                                              }),
          .deadline_miss_rate = arithmetic_mean(runs,
                                                [](const DynamicRunMetrics& metrics) {
                                                  return metrics.total_tasks == 0
                                                             ? 0.0
                                                             : static_cast<double>(metrics.deadline_misses) /
                                                                   static_cast<double>(metrics.total_tasks);
                                                }),
          .constraint_violation_rate = arithmetic_mean(
              runs,
              [](const DynamicRunMetrics& metrics) {
                return metrics.dispatched_assignments == 0 ? 0.0
                                                           : static_cast<double>(metrics.constraint_violations) /
                                                                 static_cast<double>(metrics.dispatched_assignments);
              }),
          .mean_tardiness = arithmetic_mean(
              runs, [](const DynamicRunMetrics& metrics) { return static_cast<double>(metrics.total_tardiness); }),
          .mean_makespan = arithmetic_mean(
              runs, [](const DynamicRunMetrics& metrics) { return static_cast<double>(metrics.makespan); }),
          .mean_utilization =
              arithmetic_mean(runs, [](const DynamicRunMetrics& metrics) { return metrics.utilization_ratio; }),
          .mean_assignment_churn =
              arithmetic_mean(runs, [](const DynamicRunMetrics& metrics) { return metrics.mean_assignment_churn; }),
          .mean_replans = arithmetic_mean(
              runs, [](const DynamicRunMetrics& metrics) { return static_cast<double>(metrics.replan_rounds); }),
          .preferred_actor_hit_ratio =
              arithmetic_mean(runs,
                              [](const DynamicRunMetrics& metrics) {
                                return metrics.preferred_actor_opportunities == 0
                                           ? 0.0
                                           : static_cast<double>(metrics.preferred_actor_hits) /
                                                 static_cast<double>(metrics.preferred_actor_opportunities);
                              }),
          .mean_travel_distance = arithmetic_mean(
              runs,
              [](const DynamicRunMetrics& metrics) {
                return metrics.dispatched_assignments == 0
                           ? 0.0
                           : metrics.total_travel_distance / static_cast<double>(metrics.dispatched_assignments);
              }),
          .mean_execution_cost = arithmetic_mean(runs,
                                                 [](const DynamicRunMetrics& metrics) {
                                                   return metrics.dispatched_assignments == 0
                                                              ? 0.0
                                                              : metrics.total_execution_cost /
                                                                    static_cast<double>(metrics.dispatched_assignments);
                                                 }),
      });
    }
  }
  return rows;
}

const StrategyReportRow* best_strategy_row(const std::vector<StrategyReportRow>& rows,
                                           const std::string& scenario_name) {
  const auto score = [](const StrategyReportRow& row) {
    switch (row.goal) {
      case ScenarioGoal::Throughput:
        return std::tuple(row.ok ? 1 : 0,
                          -row.constraint_violation_rate,
                          row.completion_ratio,
                          -row.mean_makespan,
                          -row.mean_latency_ns);
      case ScenarioGoal::Deadlines:
        return std::tuple(row.ok ? 1 : 0,
                          -row.constraint_violation_rate,
                          -row.deadline_miss_rate,
                          -row.mean_tardiness,
                          row.completion_ratio);
      case ScenarioGoal::Stability:
        return std::tuple(row.ok ? 1 : 0,
                          -row.constraint_violation_rate,
                          -row.mean_assignment_churn,
                          row.completion_ratio,
                          -row.mean_replans);
    }
    return std::tuple(0, 0.0, 0.0, 0.0, 0.0);
  };

  const StrategyReportRow* best_row = nullptr;
  for (const StrategyReportRow& row : rows) {
    if (row.scenario_name != scenario_name) {
      continue;
    }
    if (best_row == nullptr || score(row) > score(*best_row)) {
      best_row = &row;
    }
  }
  return best_row;
}

std::string render_markdown_report(const StrategyBenchmarkConfig& config,
                                   const std::vector<StrategyReportRow>& strategy_rows) {
  std::ostringstream report;
  report << "# Runtime Scheduling Strategy Benchmark Report\n\n";
  report << "Seed: `" << config.seed << "`. Iterations per scenario: `" << config.iterations << "`.\n";
  if (!config.scenario_filter.empty()) {
    report << "Scenario filter: `" << config.scenario_filter << "`.\n";
  }
  report << "\n";
  report << "## Criteria verified\n\n";
  report << "- `mean_latency_ns` / `p95_latency_ns`: planning responsiveness and latency tail behavior.\n";
  report << "- `completion_ratio` and `mean_makespan`: throughput and runtime completion quality.\n";
  report << "- `deadline_miss_rate` and `mean_tardiness`: deadline resilience during disruption.\n";
  report
      << "- `constraint_violation_rate`: share of dispatched assignments that violated runtime feasibility checks.\n";
  report << "- `mean_assignment_churn` and `mean_replans`: plan stability under replanning pressure.\n";
  report
      << "- `mean_utilization`: actor busy-capacity divided by total declared actor capacity in dynamic scenarios.\n";
  report << "- `preferred_actor_hit_ratio`, `mean_travel_distance`, and `mean_execution_cost`: affinity, locality, and "
            "cost quality under runtime replanning.\n\n";

  report << "## Strategy scenarios\n\n";
  report << "| Scenario | Goal | Chaotic | Strategy | Mean latency (ns) | P95 latency (ns) | Completion ratio | "
            "Deadline miss rate | Constraint violation rate | Mean tardiness | Mean makespan | Utilization | "
            "Preferred actor hit ratio | Mean travel distance | Mean execution cost | Assignment churn | Replans | "
            "OK |\n";
  report << "|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n";
  for (const StrategyReportRow& row : strategy_rows) {
    report << "|" << row.scenario_name << "|" << goal_name(row.goal) << "|" << (row.chaotic ? "yes" : "no") << "|"
           << row.strategy_name << "|" << std::fixed << std::setprecision(2) << row.mean_latency_ns << "|"
           << row.p95_latency_ns << "|" << row.completion_ratio << "|" << row.deadline_miss_rate << "|"
           << row.constraint_violation_rate << "|" << row.mean_tardiness << "|" << row.mean_makespan << "|"
           << row.mean_utilization << "|" << row.preferred_actor_hit_ratio << "|" << row.mean_travel_distance << "|"
           << row.mean_execution_cost << "|" << row.mean_assignment_churn << "|" << row.mean_replans << "|"
           << (row.ok ? "yes" : "no") << "|\n";
  }

  report << "\n## Strategy recommendations\n\n";
  for (const DynamicScenarioDefinition& scenario : dynamic_scenarios()) {
    if (!matches_filter(scenario.name, config.scenario_filter)) {
      continue;
    }
    if (const StrategyReportRow* best_row = best_strategy_row(strategy_rows, scenario.name); best_row != nullptr) {
      report << "- `" << scenario.name << "`: **" << best_row->strategy_name << "** best matched the `"
             << goal_name(scenario.goal) << "` criterion in this run. " << scenario.description << "\n";
    }
  }
  return report.str();
}

std::string render_csv_report(const std::vector<StrategyReportRow>& strategy_rows) {
  std::ostringstream csv;
  csv << "scenario,goal,chaotic,strategy,mean_latency_ns,p95_latency_ns,completion_ratio,deadline_miss_rate,"
         "constraint_violation_rate,mean_tardiness,mean_makespan,mean_utilization,preferred_actor_hit_ratio,"
         "mean_travel_distance,mean_execution_cost,mean_assignment_churn,mean_replans,ok\n";
  for (const StrategyReportRow& row : strategy_rows) {
    csv << row.scenario_name << "," << goal_name(row.goal) << "," << (row.chaotic ? "yes" : "no") << ","
        << row.strategy_name << "," << row.mean_latency_ns << "," << row.p95_latency_ns << "," << row.completion_ratio
        << "," << row.deadline_miss_rate << "," << row.constraint_violation_rate << "," << row.mean_tardiness << ","
        << row.mean_makespan << "," << row.mean_utilization << "," << row.preferred_actor_hit_ratio << ","
        << row.mean_travel_distance << "," << row.mean_execution_cost << "," << row.mean_assignment_churn << ","
        << row.mean_replans << "," << (row.ok ? "yes" : "no") << "\n";
  }
  return csv.str();
}

BenchmarkArtifacts run_benchmarks(const StrategyBenchmarkConfig& config) {
  const std::vector<StrategyReportRow> strategy_rows = benchmark_strategies(config);
  return BenchmarkArtifacts{
      .markdown_report = render_markdown_report(config, strategy_rows),
      .csv_report = render_csv_report(strategy_rows),
  };
}

bool write_report_file(const std::string& output_path, const std::string& content) {
  if (output_path.empty()) {
    return true;
  }
  const std::filesystem::path report_path(output_path);
  if (const std::filesystem::path parent = report_path.parent_path(); !parent.empty()) {
    std::error_code error_code;
    std::filesystem::create_directories(parent, error_code);
    if (error_code) {
      return false;
    }
  }
  std::ofstream out(output_path);
  if (!out) {
    return false;
  }
  out << content;
  return true;
}

StrategyBenchmarkConfig parse_config(int argc, char** argv) {
  StrategyBenchmarkConfig config;
  config.markdown_output_path = kDefaultMarkdownReportPath;
  config.csv_output_path = kDefaultCsvReportPath;
  for (int arg_index = 1; arg_index < argc; ++arg_index) {
    const std::string_view argument(argv[arg_index]);
    if (starts_with(argument, "--seed=")) {
      const std::string_view seed_text = argument.substr(std::string_view("--seed=").size());
      std::from_chars(seed_text.data(), seed_text.data() + seed_text.size(), config.seed);
    } else if (starts_with(argument, "--iterations=")) {
      int parsed_iterations = config.iterations;
      const std::string_view iteration_text = argument.substr(std::string_view("--iterations=").size());
      std::from_chars(iteration_text.data(), iteration_text.data() + iteration_text.size(), parsed_iterations);
      config.iterations = std::max(kMinimumIterations, parsed_iterations);
    } else if (starts_with(argument, "--scenario=")) {
      config.scenario_filter = std::string(argument.substr(std::string_view("--scenario=").size()));
    } else if (starts_with(argument, "--markdown=")) {
      config.markdown_output_path = std::string(argument.substr(std::string_view("--markdown=").size()));
    } else if (starts_with(argument, "--csv=")) {
      config.csv_output_path = std::string(argument.substr(std::string_view("--csv=").size()));
    } else {
      char* end = nullptr;
      const uint64_t parsed_seed = std::strtoull(argv[arg_index], &end, 10);
      if (end != argv[arg_index] && *end == '\0') {
        config.seed = parsed_seed;
      }
    }
  }
  return config;
}

}  // namespace

int main(int argc, char** argv) {
  const StrategyBenchmarkConfig config = parse_config(argc, argv);
  const BenchmarkArtifacts artifacts = run_benchmarks(config);

  if (!write_report_file(config.markdown_output_path, artifacts.markdown_report)) {
    std::fprintf(stderr, "Failed to write markdown report to %s\n", config.markdown_output_path.c_str());
    return 1;
  }
  if (!write_report_file(config.csv_output_path, artifacts.csv_report)) {
    std::fprintf(stderr, "Failed to write CSV report to %s\n", config.csv_output_path.c_str());
    return 1;
  }

  std::fputs(artifacts.markdown_report.c_str(), stdout);
  std::fputs("\n\nCSV summary:\n", stdout);
  std::fputs(artifacts.csv_report.c_str(), stdout);
  return 0;
}
