# Benchmarking

This package contains two separate benchmark entry points:

- `runtime_strategy_benchmark` for runtime scheduler behavior under replanning pressure
- `optimizer_backend_benchmark` for solver-backend behavior under global planning objectives

The goal is not just to measure raw latency. Each benchmark is named around the criterion it is trying to verify so the report answers a planning question, such as whether a strategy or backend preserves throughput, deadline quality, stability, and cost-related preferences when the workflow becomes noisy.

## What the benchmark measures

The benchmarks produce Markdown and CSV reports with the following KPI groups:

| KPI | Meaning |
|---|---|
| `mean_latency_ns`, `p95_latency_ns` | Planning or solve latency |
| `completion_ratio`, `fulfillment_ratio` | Share of work completed or assigned |
| `deadline_miss_rate`, `mean_tardiness` | Deadline quality under pressure |
| `mean_makespan` | Time to finish the scheduled workload |
| `mean_utilization` | Busy capacity divided by total declared actor capacity in dynamic scenarios |
| `mean_assignment_churn` | How much the future plan changes across replans |
| `mean_replans` | How often the scenario triggers replanning |
| `preferred_actor_hit_ratio` | How often a backend honors preferred actors |
| `mean_travel_distance`, `mean_execution_cost` | Cost-oriented optimizer quality signals |

## Scenario families

### Runtime scheduler scenarios

These simulate the runtime layer over time, including releases, failures, actor outages, and dependent phases.

| Scenario | Goal | What it stresses |
|---|---|---|
| `throughput_release_contention_responsiveness` | Throughput | Reaction speed and throughput quality under rolling releases |
| `deadline_resilience_with_actor_flapping` | Deadlines | Deadline protection under temporary capacity loss |
| `replanning_stability_under_resumable_failures` | Stability | Plan stability when assigned work fails and returns |
| `dependency_flow_stability_under_multiphase_disruption` | Stability | Cross-phase stability when dependencies and disruption interact |

### Optimizer backend scenarios

These exercise the global planning layer with static but constraint-rich models.

| Scenario | Goal | What it stresses |
|---|---|---|
| `throughput_maximization_parallel_fulfillment` | Throughput | Fulfillment speed and makespan quality when many options are feasible |
| `deadline_feasibility_under_precedence_pressure` | Deadlines | Feasibility under precedence and tight due dates |
| `cost_efficiency_with_preferred_actor_tradeoffs` | Cost | Preferred actors, travel distance, and execution cost tradeoffs |
| `overload_resilience_with_partial_fulfillment` | Throughput | Graceful degradation when demand exceeds capacity |

## Running the benchmark

### Runtime strategy benchmark

Use an optimized build for stable latency numbers and at least `10` iterations per scenario.

```bash
bazel build -c opt //task_orchestrator/benchmark:runtime_strategy_benchmark

./bazel-bin/task_orchestrator/benchmark/runtime_strategy_benchmark \
  --iterations=10 \
  --seed=20260328 \
  --markdown=task_orchestrator/benchmark/results/runtime_strategy_latest_report.md \
  --csv=task_orchestrator/benchmark/results/runtime_strategy_latest_report.csv
```

### Optimizer backend benchmark

Use an optimized build for solver comparisons. This target links the internal backend and the optional external backends.

```bash
bazel build -c opt //task_orchestrator/benchmark:optimizer_backend_benchmark

./bazel-bin/task_orchestrator/benchmark/optimizer_backend_benchmark \
  --iterations=10 \
  --seed=20260328 \
  --markdown=task_orchestrator/benchmark/results/optimizer_backend_latest_report.md \
  --csv=task_orchestrator/benchmark/results/optimizer_backend_latest_report.csv
```

If you run `bazel test` or a non-optimized `bazel build` in between, rerun the `-c opt` build before executing the binary. The `bazel-bin` symlink follows the most recent Bazel configuration, so it is easy to benchmark a fastbuild binary by accident.

The commercial MIP adapter prefers Gurobi if present, but in this environment it falls back to SCIP through OR-Tools. If the build reports that `@or_tools` is missing, the external solver dependency chain is not available in the local module graph.

### Focused runs

Run one runtime scenario family:

```bash
./bazel-bin/task_orchestrator/benchmark/runtime_strategy_benchmark \
  --scenario=dependency_flow_stability_under_multiphase_disruption \
  --iterations=10
```

Run one optimizer scenario family:

```bash
./bazel-bin/task_orchestrator/benchmark/optimizer_backend_benchmark \
  --scenario=cost_efficiency_with_preferred_actor_tradeoffs \
  --iterations=10
```

## Interpreting results

Use the benchmark according to the decision you are making:

- Choose a scheduler strategy by looking first at `completion_ratio`, `deadline_miss_rate`, and `mean_assignment_churn`, then use latency as a tiebreaker.
- Choose an optimizer backend by matching the scenario goal to the quality metrics that matter most for that workflow.
- Treat `mean_latency_ns` as a budget signal, not the only winner criterion. A solver that is faster but misses deadlines or violates cost intent is usually the wrong production choice.

Generated reports are written under [task_orchestrator/benchmark/results](/home/batr/Documents/repos/task_orchestrator/task_orchestrator/benchmark/results). CSV outputs are ignored by Git.

## Current sample observations

From the latest seeded runs in this repository:

- `PriorityOnly` was strongest in the release-contention throughput scenario.
- `EDF` was strongest in the deadline-resilience scenario.
- `SJF` was strongest in the failure-replanning and multi-phase stability scenarios.
- `indexed_branch_and_bound` won the current throughput-maximization and cost-tradeoff backend scenarios.
- `commercial_mip[SCIP]` was the strongest choice in the precedence-heavy deadline scenario in this environment.
- `ortools_cp_sat` participated successfully in all four backend scenarios in the optimized run and was the strongest choice in the overload-resilience scenario.
