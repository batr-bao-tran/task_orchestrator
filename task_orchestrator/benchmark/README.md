# Scheduling strategy benchmarks

This directory benchmarks the **efficiency of each scheduling strategy** (EDF, FIFO, SJF, PriorityOnly) on **10 complex scenarios** with **random parametrization**. Each scenario is run multiple times with different seeds; results are aggregated (average time, average assignments).

## How to run

```bash
# From repo root
bazel run //task_orchestrator/benchmark:strategy_benchmark

# With custom RNG seed (default 12345)
bazel run //task_orchestrator/benchmark:strategy_benchmark -- 42
```

Output is CSV to stdout: `Scenario,Strategy,AvgNs,AvgAssignments,Ok`.

## Scenarios (10)

| Scenario | Description |
|----------|-------------|
| `single_phase_many_tasks` | Single phase, 20–50 tasks, high contention |
| `multi_phase_dag` | Multi-phase DAG (a→b,c→d), mixed priorities |
| `many_short_tasks` | 15–35 short tasks (throughput) |
| `long_tasks_deadlines` | 4 long tasks with deadlines |
| `subprocess_heavy` | Processes with sub-processes (M1a, M1b, M2a) |
| `random_prio_duration` | 10–25 tasks, random priority/duration/deadline |
| `linear_chain` | Linear chain of 3–6 phases |
| `mixed_process_counts` | 5 processes, mixed durations |
| `two_phase_dep` | Two phases with dependency (ph1 → ph2) |
| `large_workflow` | 5–10 phases, 3–7 processes per phase |

Workflow and actor counts/capacities are parametrized by seed so each run can vary.

## Benchmark results (sample run)

Seed: `42`. Each scenario × strategy combination ran 5 times; averages below.

| Scenario | Strategy | AvgNs | AvgAssignments | Ok |
|----------|----------|-------|----------------|----|
| single_phase_many_tasks | EDF | 239273 | 11 | 1 |
| single_phase_many_tasks | FIFO | 252097 | 11 | 1 |
| single_phase_many_tasks | SJF | 239678 | 11 | 1 |
| single_phase_many_tasks | PriorityOnly | 227513 | 11 | 1 |
| multi_phase_dag | EDF | 22179 | 2 | 1 |
| multi_phase_dag | FIFO | 20721 | 2 | 1 |
| multi_phase_dag | SJF | 20349 | 2 | 1 |
| multi_phase_dag | PriorityOnly | 20171 | 2 | 1 |
| many_short_tasks | EDF | 157484 | 11 | 1 |
| many_short_tasks | FIFO | 178317 | 11 | 1 |
| many_short_tasks | SJF | 173030 | 11 | 1 |
| many_short_tasks | PriorityOnly | 174678 | 11 | 1 |
| long_tasks_deadlines | EDF | 41652 | 4 | 1 |
| long_tasks_deadlines | FIFO | 39868 | 4 | 1 |
| long_tasks_deadlines | SJF | 39613 | 4 | 1 |
| long_tasks_deadlines | PriorityOnly | 47880 | 4 | 1 |
| subprocess_heavy | EDF | 46019 | 5 | 1 |
| subprocess_heavy | FIFO | 48900 | 5 | 1 |
| subprocess_heavy | SJF | 44735 | 5 | 1 |
| subprocess_heavy | PriorityOnly | 43630 | 5 | 1 |
| random_prio_duration | EDF | 112153 | 11 | 1 |
| random_prio_duration | FIFO | 118491 | 11 | 1 |
| random_prio_duration | SJF | 109934 | 11 | 1 |
| random_prio_duration | PriorityOnly | 105241 | 11 | 1 |
| linear_chain | EDF | 21209 | 2 | 1 |
| linear_chain | FIFO | 20127 | 2 | 1 |
| linear_chain | SJF | 17779 | 2 | 1 |
| linear_chain | PriorityOnly | 17578 | 2 | 1 |
| mixed_process_counts | EDF | 35165 | 5 | 1 |
| mixed_process_counts | FIFO | 35822 | 5 | 1 |
| mixed_process_counts | SJF | 34264 | 5 | 1 |
| mixed_process_counts | PriorityOnly | 35005 | 5 | 1 |
| two_phase_dep | EDF | 26459 | 3 | 1 |
| two_phase_dep | FIFO | 25557 | 3 | 1 |
| two_phase_dep | SJF | 25410 | 3 | 1 |
| two_phase_dep | PriorityOnly | 25416 | 3 | 1 |
| large_workflow | EDF | 40649 | 5 | 1 |
| large_workflow | FIFO | 38816 | 5 | 1 |
| large_workflow | SJF | 39409 | 5 | 1 |
| large_workflow | PriorityOnly | 39428 | 5 | 1 |

**Notes**

- **AvgNs**: Average time per `Scheduler::plan()` call in nanoseconds (lower = faster).
- **AvgAssignments**: Average number of tasks assigned in that scenario/strategy (same workflow and actors, so fulfillment is comparable across strategies for a given scenario).
- All strategies complete successfully (Ok=1). Fulfillment (assignment count) is identical for a given scenario because the same workflow state and actor set are used; only the **order** in which tasks are considered differs, and the greedy assignment loop assigns as many as it can. For scenarios where capacity is sufficient, all strategies yield the same assignment count; timing differences reflect the cost of different ordering logic.

To compare strategies under different conditions, re-run with other seeds or extend the benchmark to report additional metrics (e.g. deadline misses if simulated over time).
