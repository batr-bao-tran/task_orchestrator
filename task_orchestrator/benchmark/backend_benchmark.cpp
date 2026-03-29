#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "task_orchestrator/optimizer/optimizer.hpp"

namespace to = task_orchestrator;
namespace too = task_orchestrator::optimizer;

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

constexpr uint64_t kDefaultSeed = 12345U;
constexpr int kMinimumIterations = 10;
constexpr int kDefaultIterations = kMinimumIterations;
constexpr int64_t kBackendIterationSeedStride = 211;
constexpr std::string_view kDefaultMarkdownReportPath =
    "task_orchestrator/benchmark/results/optimizer_backend_latest_report.md";
constexpr std::string_view kDefaultCsvReportPath =
    "task_orchestrator/benchmark/results/optimizer_backend_latest_report.csv";

enum class ScenarioGoal {
  Throughput,
  Deadlines,
  Cost,
};

struct BackendBenchmarkConfig {
  uint64_t seed = kDefaultSeed;
  int iterations = kDefaultIterations;
  std::string scenario_filter;
  std::string markdown_output_path;
  std::string csv_output_path;
};

struct BackendScenarioInput {
  too::OptimizationModel model;
  too::OptimizationOptions options;
};

struct BackendScenarioDefinition {
  std::string name;
  std::string description;
  ScenarioGoal goal = ScenarioGoal::Throughput;
  std::function<BackendScenarioInput(uint64_t)> build;
};

struct BackendRunMetrics {
  bool ok = true;
  size_t total_tasks = 0;
  size_t fulfilled_tasks = 0;
  size_t deadline_misses = 0;
  size_t preferred_actor_hits = 0;
  size_t preferred_actor_opportunities = 0;
  int64_t total_tardiness = 0;
  int64_t total_travel_distance = 0;
  double total_execution_cost = 0.0;
  to::Time makespan = 0;
  std::vector<int64_t> solve_latencies_ns;
  std::string configured_backend_name;
  std::string actual_backend_name;
  std::string error_message;
};

struct BackendReportRow {
  std::string scenario_name;
  std::string description;
  std::string backend_name;
  ScenarioGoal goal = ScenarioGoal::Throughput;
  bool ok = true;
  double mean_latency_ns = 0.0;
  double p95_latency_ns = 0.0;
  double fulfillment_ratio = 0.0;
  double deadline_miss_rate = 0.0;
  double mean_tardiness = 0.0;
  double mean_makespan = 0.0;
  double preferred_actor_hit_ratio = 0.0;
  double mean_travel_distance = 0.0;
  double mean_execution_cost = 0.0;
  std::string notes;
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
    case ScenarioGoal::Cost:
      return "cost";
  }
  return "unknown";
}

bool starts_with(const std::string_view value, const std::string_view prefix) {
  return value.size() >= prefix.size() && value.starts_with(prefix);
}

bool matches_filter(const std::string& value, const std::string& filter) {
  return filter.empty() || value.find(filter) != std::string::npos;
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

std::vector<to::AvailabilityWindow> availability_windows(std::initializer_list<to::AvailabilityWindow> windows) {
  return {windows};
}

BackendRunMetrics run_backend_scenario(const BackendScenarioInput& scenario,
                                       const too::BackendDescriptor& backend_descriptor) {
  BackendRunMetrics metrics;
  metrics.total_tasks = scenario.model.tasks.size();
  metrics.configured_backend_name = backend_descriptor.name;

  if (!backend_descriptor.available) {
    metrics.ok = false;
    metrics.actual_backend_name = backend_descriptor.name;
    metrics.error_message = "Backend is not linked into this benchmark binary.";
    return metrics;
  }

  too::OptimizationOptions options = scenario.options;
  options.backend_kind = backend_descriptor.kind;
  const too::WorkflowOptimizer optimizer(options);
  const auto solve_start = Clock::now();
  const too::OptimizationSolution solution = optimizer.optimize(scenario.model);
  const auto solve_end = Clock::now();
  metrics.solve_latencies_ns.push_back(std::chrono::duration_cast<Nanoseconds>(solve_end - solve_start).count());
  metrics.ok = solution.ok;
  metrics.actual_backend_name = solution.backend_name.empty() ? backend_descriptor.name : solution.backend_name;
  metrics.error_message = solution.error_message;
  metrics.fulfilled_tasks = solution.assignments.size();

  std::unordered_map<to::TaskId, const too::OptimizerTask*> tasks_by_id;
  std::unordered_map<to::ActorId, const too::OptimizerActor*> actors_by_id;
  tasks_by_id.reserve(scenario.model.tasks.size());
  actors_by_id.reserve(scenario.model.actors.size());
  for (const too::OptimizerTask& task : scenario.model.tasks) {
    tasks_by_id.emplace(task.id, &task);
  }
  for (const too::OptimizerActor& actor : scenario.model.actors) {
    actors_by_id.emplace(actor.id, &actor);
  }

  for (const too::OptimizedAssignment& assignment : solution.assignments) {
    const too::OptimizerTask* task = tasks_by_id.at(assignment.task_id);
    metrics.makespan = std::max(metrics.makespan, assignment.end_time);
    if (task->deadline && assignment.end_time > *task->deadline) {
      ++metrics.deadline_misses;
      metrics.total_tardiness += assignment.end_time - *task->deadline;
    }
    if (!task->preferred_actor_ids.empty()) {
      ++metrics.preferred_actor_opportunities;
      if (std::ranges::find(task->preferred_actor_ids, assignment.actor_id) != task->preferred_actor_ids.end()) {
        ++metrics.preferred_actor_hits;
      }
    }
    if (const auto distance_it = task->actor_distances.find(assignment.actor_id);
        distance_it != task->actor_distances.end()) {
      metrics.total_travel_distance += distance_it->second;
    }
    if (const auto actor_it = actors_by_id.find(assignment.actor_id); actor_it != actors_by_id.end()) {
      metrics.total_execution_cost += actor_it->second->execution_cost_per_unit * static_cast<double>(task->duration);
    }
  }
  return metrics;
}

std::vector<BackendScenarioDefinition> backend_scenarios() {
  struct ThroughputVariant {
    std::string suffix;
    int actor_count = 0;
    int task_count = 0;
    to::Time release_spread = 0;
    bool fragmented_windows = false;
  };
  struct DeadlineVariant {
    std::string suffix;
    int task_count = 0;
    to::Time base_deadline = 0;
    to::Time deadline_step = 0;
    to::Time latest_start_offset = 0;
  };
  struct CostVariant {
    std::string suffix;
    to::Time near_distance_bias = 0;
    to::Time far_distance_bias = 0;
    int64_t preferred_weight = 0;
    int64_t distance_weight = 0;
    int64_t execution_cost_weight = 0;
  };
  struct OverloadVariant {
    std::string suffix;
    int task_count = 0;
    int mandatory_task_count = 0;
    int actor_capacity = 0;
    to::Time actor_window_end = 0;
  };
  struct FragmentationVariant {
    std::string suffix;
    bool severe = false;
  };

  static const std::array<ThroughputVariant, 2> kThroughputVariants = {{
      {.suffix = "balanced_release_waves",
       .actor_count = 3,
       .task_count = 6,
       .release_spread = 6,
       .fragmented_windows = false},
      {.suffix = "fragmented_shift_windows",
       .actor_count = 4,
       .task_count = 8,
       .release_spread = 10,
       .fragmented_windows = true},
  }};
  static const std::array<DeadlineVariant, 2> kDeadlineVariants = {{
      {.suffix = "moderate_slack", .task_count = 5, .base_deadline = 9, .deadline_step = 4, .latest_start_offset = 4},
      {.suffix = "severe_slack_shock",
       .task_count = 6,
       .base_deadline = 6,
       .deadline_step = 3,
       .latest_start_offset = 2},
  }};
  static const std::array<CostVariant, 2> kCostVariants = {{
      {.suffix = "balanced_tradeoff_surface",
       .near_distance_bias = 1,
       .far_distance_bias = 8,
       .preferred_weight = 50,
       .distance_weight = -10,
       .execution_cost_weight = -5},
      {.suffix = "preference_distance_shock",
       .near_distance_bias = 2,
       .far_distance_bias = 11,
       .preferred_weight = 90,
       .distance_weight = -14,
       .execution_cost_weight = -3},
  }};
  static const std::array<OverloadVariant, 2> kOverloadVariants = {{
      {.suffix = "moderate_overload",
       .task_count = 7,
       .mandatory_task_count = 4,
       .actor_capacity = 1,
       .actor_window_end = 30},
      {.suffix = "hard_overload",
       .task_count = 9,
       .mandatory_task_count = 5,
       .actor_capacity = 1,
       .actor_window_end = 26},
  }};
  static const std::array<FragmentationVariant, 2> kFragmentationVariants = {{
      {.suffix = "staggered_shift_caps", .severe = false},
      {.suffix = "severe_capability_fragmentation", .severe = true},
  }};

  std::vector<BackendScenarioDefinition> scenarios;
  scenarios.reserve(kThroughputVariants.size() + kDeadlineVariants.size() + kCostVariants.size() +
                    kOverloadVariants.size() + kFragmentationVariants.size());

  for (const ThroughputVariant& variant : kThroughputVariants) {
    scenarios.push_back(BackendScenarioDefinition{
        .name = "throughput_maximization_parallel_fulfillment_" + variant.suffix,
        .description = "Verifies whether the optimizer maximizes fulfillment speed and makespan quality under release "
                       "waves and actor-window contention.",
        .goal = ScenarioGoal::Throughput,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              BackendScenarioInput input;
              input.options.allow_partial_plan = false;
              input.options.time_limit_ms = 250;
              input.options.num_search_workers = 1;
              input.model.id = "throughput_maximization_parallel_fulfillment_" + variant.suffix;
              for (int actor_index = 0; actor_index < variant.actor_count; ++actor_index) {
                const to::Time shift_split = 20 + static_cast<to::Time>(actor_index * 3);
                input.model.actors.push_back(too::OptimizerActor{
                    .id = "robot_" + std::to_string(actor_index),
                    .type = "robot",
                    .capacity = 1 + static_cast<int>(rng() % 2),
                    .availability_windows = variant.fragmented_windows
                                                ? availability_windows({
                                                      to::AvailabilityWindow{.start = 0, .end = shift_split},
                                                      to::AvailabilityWindow{.start = shift_split + 4, .end = 60},
                                                  })
                                                : availability_windows({
                                                      to::AvailabilityWindow{.start = 0, .end = 60},
                                                  }),
                    .capabilities = {"lift"},
                    .execution_cost_per_unit = 1.0 + static_cast<double>(actor_index),
                });
              }
              for (int task_index = 0; task_index < variant.task_count; ++task_index) {
                input.model.tasks.push_back(too::OptimizerTask{
                    .id = "throughput_task_" + std::to_string(task_index),
                    .duration = 2 + static_cast<to::Duration>(rng() % 4),
                    .release_time = static_cast<to::Time>(rng() % variant.release_spread),
                    .latest_start_time = std::nullopt,
                    .deadline = 22 + static_cast<to::Time>(task_index * 2),
                    .priority = 6 + static_cast<to::Priority>(task_index % 5),
                    .demand = 1,
                    .mandatory = true,
                    .preemptible = false,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {},
                    .preferred_actor_ids = {},
                    .required_capabilities = {"lift"},
                    .dependency_task_ids =
                        task_index >= 4 && variant.fragmented_windows
                            ? std::vector<to::TaskId>{"throughput_task_" + std::to_string(task_index - 4)}
                            : std::vector<to::TaskId>{},
                    .mutually_exclusive_task_ids = {},
                    .actor_distances = {},
                    .tardiness_cost_per_unit = 0.0,
                    .early_start_bonus = 0.0,
                });
              }
              return input;
            },
    });
  }

  for (const DeadlineVariant& variant : kDeadlineVariants) {
    scenarios.push_back(BackendScenarioDefinition{
        .name = "deadline_feasibility_under_precedence_pressure_" + variant.suffix,
        .description = "Verifies deadline feasibility, precedence handling, and preferred-actor quality when slack is "
                       "compressed by chained work.",
        .goal = ScenarioGoal::Deadlines,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              BackendScenarioInput input;
              input.options.allow_partial_plan = false;
              input.options.time_limit_ms = 250;
              input.options.num_search_workers = 1;
              input.model.id = "deadline_feasibility_under_precedence_pressure_" + variant.suffix;
              input.model.actors = {
                  too::OptimizerActor{
                      .id = "robot_fast",
                      .type = "robot",
                      .capacity = 1,
                      .availability_windows = availability_windows({to::AvailabilityWindow{.start = 0, .end = 50}}),
                      .capabilities = {"scan"},
                      .execution_cost_per_unit = 1.0},
                  too::OptimizerActor{
                      .id = "robot_backup",
                      .type = "robot",
                      .capacity = 1,
                      .availability_windows = availability_windows({to::AvailabilityWindow{.start = 0, .end = 50}}),
                      .capabilities = {"scan"},
                      .execution_cost_per_unit = 2.0},
              };
              for (int task_index = 0; task_index < variant.task_count; ++task_index) {
                std::vector<to::TaskId> dependencies;
                if (task_index > 0) {
                  dependencies.push_back("deadline_task_" + std::to_string(task_index - 1));
                }
                const to::Duration duration = 3 + static_cast<to::Duration>(rng() % 3);
                input.model.tasks.push_back(too::OptimizerTask{
                    .id = "deadline_task_" + std::to_string(task_index),
                    .duration = duration,
                    .release_time = 0,
                    .latest_start_time = variant.latest_start_offset + static_cast<to::Time>(task_index * 2),
                    .deadline = variant.base_deadline + static_cast<to::Time>(task_index * variant.deadline_step),
                    .priority = 25 - task_index,
                    .demand = 1,
                    .mandatory = true,
                    .preemptible = false,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {},
                    .preferred_actor_ids = {"robot_fast"},
                    .required_capabilities = {"scan"},
                    .dependency_task_ids = dependencies,
                    .mutually_exclusive_task_ids = {},
                    .actor_distances = {{"robot_fast", 1}, {"robot_backup", 4 + (task_index % 2)}},
                    .tardiness_cost_per_unit = 2.0,
                    .early_start_bonus = 0.0,
                });
              }
              return input;
            },
    });
  }

  for (const CostVariant& variant : kCostVariants) {
    scenarios.push_back(BackendScenarioDefinition{
        .name = "cost_efficiency_with_preferred_actor_tradeoffs_" + variant.suffix,
        .description = "Verifies whether the optimizer balances preferred actors, travel distance, and execution cost "
                       "when the objective surface is intentionally conflicted.",
        .goal = ScenarioGoal::Cost,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              BackendScenarioInput input;
              input.options.allow_partial_plan = true;
              input.options.time_limit_ms = 250;
              input.options.num_search_workers = 1;
              input.options.objective.travel_distance_weight = variant.distance_weight;
              input.options.objective.execution_cost_weight = variant.execution_cost_weight;
              input.options.objective.preferred_actor_weight = variant.preferred_weight;
              input.model.id = "cost_efficiency_with_preferred_actor_tradeoffs_" + variant.suffix;
              input.model.actors = {
                  too::OptimizerActor{
                      .id = "near_expensive",
                      .type = "robot",
                      .capacity = 1,
                      .availability_windows = availability_windows({to::AvailabilityWindow{.start = 0, .end = 70}}),
                      .capabilities = {"lift"},
                      .execution_cost_per_unit = 5.0},
                  too::OptimizerActor{
                      .id = "far_cheap",
                      .type = "robot",
                      .capacity = 1,
                      .availability_windows = availability_windows({to::AvailabilityWindow{.start = 0, .end = 70}}),
                      .capabilities = {"lift"},
                      .execution_cost_per_unit = 1.0},
              };
              for (int task_index = 0; task_index < 5; ++task_index) {
                input.model.tasks.push_back(too::OptimizerTask{
                    .id = "cost_task_" + std::to_string(task_index),
                    .duration = 4 + static_cast<to::Duration>(rng() % 3),
                    .release_time = static_cast<to::Time>(task_index % 2),
                    .latest_start_time = std::nullopt,
                    .deadline = 42,
                    .priority = 5 + static_cast<to::Priority>(task_index % 3),
                    .demand = 1,
                    .mandatory = true,
                    .preemptible = false,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {},
                    .preferred_actor_ids = {task_index % 2 == 0 ? "near_expensive" : "far_cheap"},
                    .required_capabilities = {"lift"},
                    .dependency_task_ids = {},
                    .mutually_exclusive_task_ids = {},
                    .actor_distances = {{"near_expensive", variant.near_distance_bias + task_index},
                                        {"far_cheap", variant.far_distance_bias - task_index}},
                    .tardiness_cost_per_unit = 0.0,
                    .early_start_bonus = 0.25,
                });
              }
              return input;
            },
    });
  }

  for (const OverloadVariant& variant : kOverloadVariants) {
    scenarios.push_back(BackendScenarioDefinition{
        .name = "overload_resilience_with_partial_fulfillment_" + variant.suffix,
        .description = "Verifies graceful degradation, mandatory-task protection, and partial-plan quality when demand "
                       "significantly exceeds available capacity.",
        .goal = ScenarioGoal::Throughput,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              BackendScenarioInput input;
              input.options.allow_partial_plan = true;
              input.options.time_limit_ms = 250;
              input.options.num_search_workers = 1;
              input.model.id = "overload_resilience_with_partial_fulfillment_" + variant.suffix;
              input.model.actors = {
                  too::OptimizerActor{.id = "robot_a",
                                      .type = "robot",
                                      .capacity = variant.actor_capacity,
                                      .availability_windows = availability_windows(
                                          {to::AvailabilityWindow{.start = 0, .end = variant.actor_window_end}}),
                                      .capabilities = {"pack"},
                                      .execution_cost_per_unit = 1.0},
                  too::OptimizerActor{.id = "robot_b",
                                      .type = "robot",
                                      .capacity = variant.actor_capacity,
                                      .availability_windows = availability_windows(
                                          {to::AvailabilityWindow{.start = 0, .end = variant.actor_window_end}}),
                                      .capabilities = {"pack"},
                                      .execution_cost_per_unit = 1.0},
              };
              for (int task_index = 0; task_index < variant.task_count; ++task_index) {
                input.model.tasks.push_back(too::OptimizerTask{
                    .id = "overload_task_" + std::to_string(task_index),
                    .duration = 5 + static_cast<to::Duration>(rng() % 4),
                    .release_time = static_cast<to::Time>((task_index / 3) * 2),
                    .latest_start_time = std::nullopt,
                    .deadline = 16 + static_cast<to::Time>(task_index),
                    .priority = 11 - static_cast<to::Priority>(task_index % 4),
                    .demand = task_index % 4 == 0 ? 2 : 1,
                    .mandatory = task_index < variant.mandatory_task_count,
                    .preemptible = false,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {},
                    .preferred_actor_ids = {},
                    .required_capabilities = {"pack"},
                    .dependency_task_ids =
                        task_index >= 5 ? std::vector<to::TaskId>{"overload_task_" + std::to_string(task_index - 5)}
                                        : std::vector<to::TaskId>{},
                    .mutually_exclusive_task_ids = {},
                    .actor_distances = {},
                    .tardiness_cost_per_unit = 1.0,
                    .early_start_bonus = 0.0,
                });
              }
              return input;
            },
    });
  }

  for (const FragmentationVariant& variant : kFragmentationVariants) {
    scenarios.push_back(BackendScenarioDefinition{
        .name = "capability_fragmentation_under_shift_gap_pressure_" + variant.suffix,
        .description = "Verifies whether the optimizer can recover throughput when capabilities are split across "
                       "staggered shifts and dependency chains.",
        .goal = ScenarioGoal::Throughput,
        .build =
            [variant](const uint64_t seed) {
              std::mt19937 rng(static_cast<unsigned>(seed));
              BackendScenarioInput input;
              input.options.allow_partial_plan = true;
              input.options.time_limit_ms = 250;
              input.options.num_search_workers = 1;
              input.model.id = "capability_fragmentation_under_shift_gap_pressure_" + variant.suffix;
              input.model.actors = {
                  too::OptimizerActor{.id = "scan_day",
                                      .type = "robot",
                                      .capacity = 1,
                                      .availability_windows = variant.severe
                                                                  ? availability_windows({
                                                                        to::AvailabilityWindow{.start = 0, .end = 10},
                                                                        to::AvailabilityWindow{.start = 18, .end = 28},
                                                                    })
                                                                  : availability_windows({
                                                                        to::AvailabilityWindow{.start = 0, .end = 14},
                                                                        to::AvailabilityWindow{.start = 18, .end = 32},
                                                                    }),
                                      .capabilities = {"scan"},
                                      .execution_cost_per_unit = 1.0},
                  too::OptimizerActor{.id = "lift_day",
                                      .type = "robot",
                                      .capacity = 1,
                                      .availability_windows = variant.severe
                                                                  ? availability_windows({
                                                                        to::AvailabilityWindow{.start = 6, .end = 16},
                                                                    })
                                                                  : availability_windows({
                                                                        to::AvailabilityWindow{.start = 4, .end = 20},
                                                                    }),
                                      .capabilities = {"lift"},
                                      .execution_cost_per_unit = 1.5},
                  too::OptimizerActor{
                      .id = "hybrid_night",
                      .type = "robot",
                      .capacity = 1,
                      .availability_windows = availability_windows({to::AvailabilityWindow{.start = 16, .end = 40}}),
                      .capabilities = {"scan", "lift"},
                      .execution_cost_per_unit = 2.0},
              };
              for (int task_index = 0; task_index < 6; ++task_index) {
                const bool is_scan_stage = task_index % 2 == 0;
                std::vector<to::TaskId> dependencies;
                if (!is_scan_stage) {
                  dependencies.push_back("fragment_task_" + std::to_string(task_index - 1));
                }
                input.model.tasks.push_back(too::OptimizerTask{
                    .id = "fragment_task_" + std::to_string(task_index),
                    .duration = 3 + static_cast<to::Duration>(rng() % 2),
                    .release_time = static_cast<to::Time>(task_index),
                    .latest_start_time = variant.severe ? std::optional<to::Time>(18 + task_index) : std::nullopt,
                    .deadline = 24 + static_cast<to::Time>(task_index * 2),
                    .priority = 9 - static_cast<to::Priority>(task_index % 3),
                    .demand = 1,
                    .mandatory = !variant.severe || task_index < 4,
                    .preemptible = false,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {},
                    .preferred_actor_ids =
                        is_scan_stage ? std::vector<to::ActorId>{"scan_day"} : std::vector<to::ActorId>{"lift_day"},
                    .required_capabilities = {is_scan_stage ? "scan" : "lift"},
                    .dependency_task_ids = dependencies,
                    .mutually_exclusive_task_ids =
                        variant.severe && task_index >= 4
                            ? std::vector<to::TaskId>{"fragment_task_" + std::to_string(task_index - 4)}
                            : std::vector<to::TaskId>{},
                    .actor_distances = {{"scan_day", is_scan_stage ? 1 : 5},
                                        {"lift_day", is_scan_stage ? 5 : 1},
                                        {"hybrid_night", 3}},
                    .tardiness_cost_per_unit = 1.5,
                    .early_start_bonus = 0.0,
                });
              }
              return input;
            },
    });
  }

  return scenarios;
}

const BackendReportRow* best_backend_row(const std::vector<BackendReportRow>& rows, const std::string& scenario_name) {
  const auto score = [](const BackendReportRow& row) {
    switch (row.goal) {
      case ScenarioGoal::Throughput:
        return std::tuple(row.ok ? 1 : 0, row.fulfillment_ratio, -row.mean_makespan, -row.mean_latency_ns);
      case ScenarioGoal::Deadlines:
        return std::tuple(row.ok ? 1 : 0, -row.deadline_miss_rate, -row.mean_tardiness, row.fulfillment_ratio);
      case ScenarioGoal::Cost:
        return std::tuple(
            row.ok ? 1 : 0, row.preferred_actor_hit_ratio, -row.mean_travel_distance, -row.mean_execution_cost);
    }
    return std::tuple(0, 0.0, 0.0, 0.0);
  };

  const BackendReportRow* best_row = nullptr;
  for (const BackendReportRow& row : rows) {
    if (row.scenario_name != scenario_name) {
      continue;
    }
    if (best_row == nullptr || score(row) > score(*best_row)) {
      best_row = &row;
    }
  }
  return best_row;
}

std::vector<BackendReportRow> benchmark_backends(const BackendBenchmarkConfig& config) {
  std::vector<BackendReportRow> rows;
  const std::vector<too::BackendDescriptor> supported_backends = too::list_supported_backends();
  const auto scenarios = backend_scenarios();

  for (const BackendScenarioDefinition& scenario : scenarios) {
    if (!matches_filter(scenario.name, config.scenario_filter)) {
      continue;
    }
    for (const too::BackendDescriptor& backend_descriptor : supported_backends) {
      std::vector<BackendRunMetrics> runs;
      runs.reserve(config.iterations);
      std::vector<int64_t> solve_latencies;
      for (int iteration = 0; iteration < config.iterations; ++iteration) {
        const BackendScenarioInput scenario_input =
            scenario.build(config.seed + static_cast<uint64_t>(iteration * kBackendIterationSeedStride));
        BackendRunMetrics metrics = run_backend_scenario(scenario_input, backend_descriptor);
        solve_latencies.insert(
            solve_latencies.end(), metrics.solve_latencies_ns.begin(), metrics.solve_latencies_ns.end());
        runs.push_back(std::move(metrics));
      }

      const auto representative_run_it = std::ranges::find_if(
          runs, [](const BackendRunMetrics& metrics) { return !metrics.actual_backend_name.empty(); });
      const BackendRunMetrics* representative_run =
          representative_run_it == runs.end() ? nullptr : &*representative_run_it;
      const std::string backend_label =
          representative_run == nullptr ? backend_descriptor.name : representative_run->actual_backend_name;
      const bool all_runs_ok = std::ranges::all_of(runs, [](const BackendRunMetrics& metrics) { return metrics.ok; });

      rows.push_back(BackendReportRow{
          .scenario_name = scenario.name,
          .description = scenario.description,
          .backend_name = backend_label,
          .goal = scenario.goal,
          .ok = all_runs_ok,
          .mean_latency_ns = arithmetic_mean(solve_latencies,
                                             [](const int64_t latency_ns) { return static_cast<double>(latency_ns); }),
          .p95_latency_ns = percentile(solve_latencies, 0.95),
          .fulfillment_ratio = arithmetic_mean(runs,
                                               [](const BackendRunMetrics& metrics) {
                                                 return metrics.total_tasks == 0
                                                            ? 0.0
                                                            : static_cast<double>(metrics.fulfilled_tasks) /
                                                                  static_cast<double>(metrics.total_tasks);
                                               }),
          .deadline_miss_rate = arithmetic_mean(runs,
                                                [](const BackendRunMetrics& metrics) {
                                                  return metrics.total_tasks == 0
                                                             ? 0.0
                                                             : static_cast<double>(metrics.deadline_misses) /
                                                                   static_cast<double>(metrics.total_tasks);
                                                }),
          .mean_tardiness = arithmetic_mean(
              runs, [](const BackendRunMetrics& metrics) { return static_cast<double>(metrics.total_tardiness); }),
          .mean_makespan = arithmetic_mean(
              runs, [](const BackendRunMetrics& metrics) { return static_cast<double>(metrics.makespan); }),
          .preferred_actor_hit_ratio =
              arithmetic_mean(runs,
                              [](const BackendRunMetrics& metrics) {
                                return metrics.preferred_actor_opportunities == 0
                                           ? 0.0
                                           : static_cast<double>(metrics.preferred_actor_hits) /
                                                 static_cast<double>(metrics.preferred_actor_opportunities);
                              }),
          .mean_travel_distance = arithmetic_mean(
              runs,
              [](const BackendRunMetrics& metrics) { return static_cast<double>(metrics.total_travel_distance); }),
          .mean_execution_cost =
              arithmetic_mean(runs, [](const BackendRunMetrics& metrics) { return metrics.total_execution_cost; }),
          .notes =
              [&]() {
                if (all_runs_ok) {
                  return std::string{};
                }
                if (!runs.back().error_message.empty()) {
                  return runs.back().error_message;
                }
                return std::string("Solver returned a non-optimal, unavailable, or infeasible result.");
              }(),
      });
    }
  }
  return rows;
}

std::string render_markdown_report(const BackendBenchmarkConfig& config,
                                   const std::vector<BackendReportRow>& backend_rows) {
  std::ostringstream report;
  report << "# Optimization Backend Comparison Benchmark Report\n\n";
  report << "Seed: `" << config.seed << "`. Iterations per scenario: `" << config.iterations << "`.\n";
  if (!config.scenario_filter.empty()) {
    report << "Scenario filter: `" << config.scenario_filter << "`.\n";
  }
  report << "\n";
  report << "## Criteria verified\n\n";
  report << "- `mean_latency_ns` / `p95_latency_ns`: solve latency and latency tail behavior.\n";
  report << "- `fulfillment_ratio` and `mean_makespan`: throughput and schedule-completion quality.\n";
  report << "- `deadline_miss_rate` and `mean_tardiness`: deadline-feasibility behavior under pressure.\n";
  report << "- `preferred_actor_hit_ratio`, `mean_travel_distance`, and `mean_execution_cost`: cost and preference "
            "tradeoff quality.\n\n";

  report << "## Backend scenarios\n\n";
  report << "| Scenario | Goal | Backend | Mean latency (ns) | P95 latency (ns) | Fulfillment ratio | Deadline miss "
            "rate | Mean tardiness | Mean makespan | Preferred hit ratio | Mean travel distance | Mean execution cost "
            "| OK | Notes |\n";
  report << "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|\n";
  for (const BackendReportRow& row : backend_rows) {
    report << "|" << row.scenario_name << "|" << goal_name(row.goal) << "|" << row.backend_name << "|" << std::fixed
           << std::setprecision(2) << row.mean_latency_ns << "|" << row.p95_latency_ns << "|" << row.fulfillment_ratio
           << "|" << row.deadline_miss_rate << "|" << row.mean_tardiness << "|" << row.mean_makespan << "|"
           << row.preferred_actor_hit_ratio << "|" << row.mean_travel_distance << "|" << row.mean_execution_cost << "|"
           << (row.ok ? "yes" : "no") << "|" << row.notes << "|\n";
  }

  report << "\n## Backend recommendations\n\n";
  for (const BackendScenarioDefinition& scenario : backend_scenarios()) {
    if (!matches_filter(scenario.name, config.scenario_filter)) {
      continue;
    }
    if (const BackendReportRow* best_row = best_backend_row(backend_rows, scenario.name); best_row != nullptr) {
      report << "- `" << scenario.name << "`: **" << best_row->backend_name << "** best matched the `"
             << goal_name(scenario.goal) << "` criterion in this run. " << scenario.description << "\n";
    }
  }
  return report.str();
}

std::string render_csv_report(const std::vector<BackendReportRow>& backend_rows) {
  std::ostringstream csv;
  csv << "scenario,goal,backend,mean_latency_ns,p95_latency_ns,fulfillment_ratio,deadline_miss_rate,"
         "mean_tardiness,mean_makespan,preferred_actor_hit_ratio,mean_travel_distance,mean_execution_cost,ok,notes\n";
  for (const BackendReportRow& row : backend_rows) {
    csv << row.scenario_name << "," << goal_name(row.goal) << "," << row.backend_name << "," << row.mean_latency_ns
        << "," << row.p95_latency_ns << "," << row.fulfillment_ratio << "," << row.deadline_miss_rate << ","
        << row.mean_tardiness << "," << row.mean_makespan << "," << row.preferred_actor_hit_ratio << ","
        << row.mean_travel_distance << "," << row.mean_execution_cost << "," << (row.ok ? "yes" : "no") << ","
        << row.notes << "\n";
  }
  return csv.str();
}

BenchmarkArtifacts run_benchmarks(const BackendBenchmarkConfig& config) {
  const std::vector<BackendReportRow> backend_rows = benchmark_backends(config);
  return BenchmarkArtifacts{
      .markdown_report = render_markdown_report(config, backend_rows),
      .csv_report = render_csv_report(backend_rows),
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

BackendBenchmarkConfig parse_config(int argc, char** argv) {
  BackendBenchmarkConfig config;
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
  const BackendBenchmarkConfig config = parse_config(argc, argv);
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
