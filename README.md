# Task Orchestrator

A C++20 library for generic workflow coordination between multiple actors: DAG-based phases and processes, capacity- and availability-aware scheduling, and a Boost.MSM planner state machine for low-latency planning.

To use this project, either build locally with Bazel or use CI/release artifacts. See [Build Project](#build-project) and [CI and releases](#ci-and-releases).

---

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Build Project](#build-project)
- [Using the library in your project](#using-the-library-in-your-project)
- [API overview](#api-overview)
- [Usage examples](#usage-examples)
- [Testing](#testing)
- [Pre-commit](#pre-commit)
- [CI and releases](#ci-and-releases)
- [Repository layout](#repository-layout)
- [Application layer and config](#application-layer-and-config)
- [Discrete event simulation](#discrete-event-simulation)
- [Optimization and capacity](#optimization-and-capacity)
- [License](#license)

---

## Features

- **Workflows**: DAG of phases; each phase has processes and optional sub-processes.
- **Actors**: Resources with a capacity (concurrent tasks) and availability windows (uptime).
- **Scheduler**: Greedy, priority- and capacity-aware assignment (EDF-style) to maximize throughput and uptime.
- **Planner FSM**: Boost.MSM state machine (Idle → Planning → Dispatching → Running) for the planning loop.
- **Orchestrator**: Single facade to set workflow, register actors, start, tick, and complete phases.
- **C++20**: Coroutines (`Generator<Assignment>`), `std::ranges`, and template concepts.
- **Application layer**: Config-driven runner for warehouse-scale workflows; reports unfulfilled tasks and capacity issues.
- **Discrete event simulation**: `SimClock` for discrete-time event scheduling in tests and simulations.
- **lld**: Optional use of the LLVM linker (faster links); CI installs it on Linux.

---

## License

This project is **open source** under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. See [LICENSE](LICENSE) for the full text.

- You may use, modify, and distribute the software under the terms of the AGPL-3.0.
- **Copyleft**: If you distribute modified versions or provide the software as a network service, you must make the corresponding source code available under the same license. In practice, this means **changes must be shared with the community**—you cannot keep a private fork while distributing or offering the software to others. Contributing changes back upstream is the intended way to comply.

---

## Requirements

- **Bazel** 6+ (recommended 7.x)
- **C++20** toolchain (GCC 10+, Clang 12+, or MSVC 2019+)
- **Boost** 1.84 (headers only for MSM; fetched by the build when using the provided MODULE.bazel extension)

No build artifacts are stored in the repo; build output is under `bazel-*` and is ignored by Git.

---

## Build Project

Clone the repository:

```bash
git clone <repo-url> task_orchestrator
cd task_orchestrator
```

Build the library:

```bash
bazel build //task_orchestrator:task_orchestrator
```

If you see `cannot find 'ld'`, install a full C++ toolchain (e.g. `build-essential` on Debian/Ubuntu) and ensure `ld` is on your `PATH`. On Linux you can use the LLVM linker for faster builds:

```bash
sudo apt-get install lld
bazel build --config=lld //task_orchestrator:task_orchestrator
```

Build the config-driven application:

```bash
bazel build //application:run_config
```

---

## Using the library in your project

### Option A: Depend on this repo with Bazel

In your `WORKSPACE` or via Bzlmod, add this repository. Then in your `BUILD`:

```python
cc_binary(
    name = "my_app",
    srcs = ["main.cpp"],
    deps = ["@task_orchestrator//:task_orchestrator"],
)
```

Your code includes the public headers, e.g.:

```cpp
#include "task_orchestrator/core/orchestrator.hpp"
```

### Option B: Use a release tarball

From a [GitHub release](.github/workflows/release.yml), download `task_orchestrator-<version>-<platform>.tar.gz` for your platform. Each tarball contains:

- `include/task_orchestrator/` — public headers
- `lib/` — built static or shared library (if present)

Add the `include` directory to your include path and link against the library in `lib/`. No build artifacts are committed in the repo; these tarballs are produced by CI on release.

---

## API overview

### Namespace and headers

Everything lives in `namespace task_orchestrator`. Public headers under `include/task_orchestrator/`: **data/** (types, phase, process), **core/** (actor, workflow, scheduler, orchestrator, planner_fsm, concepts), **strategy/** (scheduling strategies: EDF, FIFO, SJF, priority-only), **utils/** (generator, SimClock). Utils in package `//task_orchestrator/utils:utils` with `#include "task_orchestrator/utils/sim_clock.hpp"`; generator header-only: `//task_orchestrator/utils:generator`.

| Header | Purpose |
|--------|--------|
| `data/types.hpp` | `WorkflowId`, `PhaseId`, `TaskId`, `ActorId`, `Time`, `Duration`, `Priority`, `AvailabilityWindow` |
| `data/phase.hpp` | `Phase` (id, name, process_ids, dependency_phase_ids) |
| `data/process.hpp` | `Process` (id, phase_id, sub_process_ids, estimated_duration, priority, deadline, `task_ids()`) |
| `core/actor.hpp` | `Actor` (id, capacity, availability_windows, current_load, `can_accept_at()`, `next_available_start()`) |
| `core/workflow.hpp` | `Workflow` (add_phase, add_process, phase, process, root_phases, ready_phases, task_ids_for_phase, process_for_task) |
| `core/scheduler.hpp` | `Assignment`, `WorkflowState`, `ScheduleResult`, `ActorRegistry`, `Scheduler::plan()`, `Scheduler::plan_lazy()` (optional `SchedulingStrategy*`) |
| `strategy/scheduling_strategy.hpp` | `TaskInfo`, `SchedulingStrategy` (order_tasks) |
| `strategy/edf_strategy.hpp` | `EDFStrategy` (earliest deadline first) |
| `strategy/fifo_strategy.hpp` | `FIFOStrategy` (first-in-first-out) |
| `strategy/sjf_strategy.hpp` | `SJFStrategy` (shortest job first) |
| `strategy/priority_only_strategy.hpp` | `PriorityOnlyStrategy` |
| `utils/generator.hpp` | `Generator<T>` (C++20 coroutine generator) |
| `core/concepts.hpp` | `Identifiable`, `SchedulableTask`, `HasAvailability`, `PhaseIdRange` |
| `core/planner_fsm.hpp` | `PlannerState`, events, `PlannerStateMachine` |
| `core/orchestrator.hpp` | `Orchestrator` (set_workflow, register_actor, set_scheduling_strategy, start, tick, complete_phase, get_latest_schedule, workflow_state) |

### Core types

- **Workflow**: Directed acyclic graph of **phases**. Each phase has a list of **processes**; a process can list **sub-process** IDs (all are schedulable tasks).
- **Actor**: Has `id`, `capacity` (max concurrent tasks), and `availability_windows` (time intervals when it can run work). The scheduler only assigns work inside those windows and within capacity.
- **Assignment**: A single (task_id, actor_id, start_time). Produced by `Scheduler::plan()` or yielded by `Scheduler::plan_lazy()`.
- **WorkflowState**: Tracks `completed_phases`, `assigned_tasks`, and optional `actor_load` overrides for the scheduler.

### Orchestrator lifecycle

1. **set_workflow** / **register_actor**: Configure workflow and actors.
2. **start()**: Enters planning and produces an initial schedule.
3. **tick(now)**: Advance time; the orchestrator may re-plan or dispatch.
4. **complete_phase(phase_id)**: Mark a phase done so later phases can become ready.
5. **get_latest_schedule()**: Read the current set of assignments.

---

## Usage examples

### Minimal: orchestrator only

```cpp
#include "task_orchestrator/core/orchestrator.hpp"

using namespace task_orchestrator;

int main() {
  Workflow w("my_workflow");
  w.add_phase(Phase{"phase1", "Phase 1", {"P1"}, {}});
  w.add_process(Process{"P1", "phase1", {}, 10, 0, {}});

  Orchestrator o;
  o.set_workflow(std::move(w));
  o.register_actor(Actor{"worker1", 1, {{0, 1000}}, 0});
  o.start();
  o.tick(0);

  ScheduleResult schedule = o.get_latest_schedule();
  for (Assignment const& a : schedule.assignments) {
    // use a.task_id, a.actor_id, a.start_time
  }
  return 0;
}
```

### Multi-phase DAG and multiple actors

```cpp
#include "task_orchestrator/core/orchestrator.hpp"

Workflow w("pipeline");
w.add_phase(Phase{"stage1", "Stage 1", {"P1", "P2"}, {}});
w.add_phase(Phase{"stage2", "Stage 2", {"P3"}, {"stage1"}});
w.add_process(Process{"P1", "stage1", {}, 5, 1, {}});
w.add_process(Process{"P2", "stage1", {}, 5, 1, {}});
w.add_process(Process{"P3", "stage2", {}, 10, 0, {}});

Orchestrator o;
o.set_workflow(std::move(w));
o.register_actor(Actor{"worker_a", 1, {{0, 100}}, 0});
o.register_actor(Actor{"worker_b", 1, {{0, 100}}, 0});
o.start();
o.tick(0);

// After stage1 completes in your system:
o.complete_phase("stage1");
o.tick(10);
```

### Using the scheduler directly (no orchestrator)

```cpp
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"

Workflow w("wf");
// ... add phases and processes ...

WorkflowState state;
state.completed_phases = {"phase1"};
ActorRegistry reg;
reg.add(Actor{"A1", 2, {{0, 1000}}, 0});

Scheduler sched;
ScheduleResult result = sched.plan(w, state, reg, /* now */ 0);
for (Assignment const& a : result.assignments) {
  // assign a.task_id to a.actor_id at a.start_time
}
```

### Lazy assignments with coroutines (C++20)

```cpp
#include "task_orchestrator/scheduler.hpp"

// ... build Workflow w, WorkflowState state, ActorRegistry reg ...

Scheduler sched;
for (Assignment const& a : sched.plan_lazy(w, state, reg, 0)) {
  // process one assignment at a time (lazy)
}
```

### Concepts (optional)

```cpp
#include "task_orchestrator/core/concepts.hpp"
#include "task_orchestrator/core/actor.hpp"

static_assert(task_orchestrator::Identifiable<task_orchestrator::Phase>);
static_assert(task_orchestrator::HasAvailability<task_orchestrator::Actor>);
```

---

## Testing

Tests use **Google Test** (gtest/gmock). From the repo root:

```bash
bazel test //task_orchestrator/tests/...
bazel test //application/tests/...
# or
bazel test //...
```

- **Unit tests** (`task_orchestrator/tests/unit/`): types, process, actor, workflow, scheduler, planner_fsm, orchestrator (including coroutine `plan_lazy`).
- **Scenario tests** (`task_orchestrator/tests/scenario/`): multi-actor workflows, subprocesses, throughput, uptime, FSM lifecycle.
- **Application tests** (`application/tests/`): config loader, runner, DES runner, scenario warehouse.

---

## Pre-commit

Format and lint before committing (no build artifacts are in the repo; pre-commit only touches source and config):

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
```

Hooks include: generic checks (YAML, JSON, trailing whitespace, etc.), Buildifier for BUILD files, clang-format for C++, **fix-header-guards** (rewrites include guards to `PROJECT__PATH__FILE_HPP_`), and actionlint for GitHub Actions. See `.pre-commit-config.yaml` and `.gitignore` (which excludes build output).

---

## CI and releases

- **`.github/workflows/ci.yml`**
  On push/PR to `main`/`master`: builds the library and runs unit and scenario tests on Ubuntu 22.04 and macOS 14. No build artifacts are committed; CI runs on GitHub’s runners.

- **`.github/workflows/release.yml`**
  On publishing a release (or manual dispatch): builds and tests on Linux (amd64) and macOS (arm64), then produces `task_orchestrator-<version>-<platform>.tar.gz` (headers + built library) and attaches them as **release assets**. Use those tarballs to consume the library without building from source; the repository itself still contains no committed build artifacts.

---

## Repository layout

```
task_orchestrator/
├── .github/workflows/       # CI and release workflows (no artifacts in repo)
├── task_orchestrator/
│   ├── include/task_orchestrator/   # Public headers (data, logic, strategy, utils)
│   ├── src/                         # Implementation
│   ├── benchmark/                   # Strategy benchmarks (10 scenarios, random param)
│   └── tests/
│       ├── unit/                    # Unit tests (Google Test)
│       └── scenario/                # Scenario tests (incl. strategy/)
├── third_party/             # Boost BUILD and Bzlmod extension
├── LICENSE                  # AGPL-3.0
├── .gitignore               # Excludes all build artifacts
├── .pre-commit-config.yaml
├── DESIGN.md                # Architecture and design
├── MODULE.bazel
└── WORKSPACE
```

Only source, config, and docs are committed; build output is ignored and produced locally or by CI.

---

## Application layer and config

The **application** layer lets you define workflows via a **config file** and run the scheduler for warehouse-scale setups: many tasks at given times, multiple actor types (e.g. robots, machines), and capacity/unfulfilled reporting.

### Config file format (YAML)

Workflow configs are **YAML** files with top-level keys `id`, `actors`, and `tasks`. This allows comments, clear structure, and easy extension.

- **id** — workflow identifier (string).
- **actors** — list of actors; each has `id`, `type`, `capacity`, and `windows` (list of `{ start, end }`).
- **tasks** — list of tasks; each has `id`, `requested_time`, `duration`, `deadline`, and `allowed_actor_types` (list of type names).

Example (`application/examples/warehouse_simple.yaml`):

```yaml
id: warehouse_simple
actors:
  - id: robot_1
    type: robot
    capacity: 2
    windows:
      - start: 0
        end: 1000
  - id: robot_2
    type: robot
    capacity: 1
    windows:
      - start: 0
        end: 1000
tasks:
  - id: order_1
    requested_time: 0
    duration: 30
    deadline: 100
    allowed_actor_types: [robot]
  - id: order_2
    requested_time: 10
    duration: 20
    deadline: 80
    allowed_actor_types: [robot]
```

### Running

```bash
bazel run //application:run_config -- <path_to_config>
# or
bazel run //application:run_config -- -   # read from stdin
```

Exit codes: `0` = all tasks fulfilled, `2` = capacity issue (some unfulfilled), `1` = error.

### API

- `task_orchestrator::app::WorkflowConfig` — in-memory config (actors, tasks).
- `load_config_from_file(path)` / `load_config_from_string(content)` — parse YAML config.
- `run(config)` — returns `RunResult`: `assignments`, `unfulfilled_task_ids`, `capacity_issue`.

Example workflows are under `application/examples/` as YAML (e.g. `warehouse_simple.yaml`, `warehouse_capacity_issue.yaml`, `mixed_actors.yaml`).

---

## Discrete event simulation

The library provides **SimClock** for discrete-event simulation (DES): schedule callbacks at specific times and advance the simulation.

```cpp
#include "task_orchestrator/utils/sim_clock.hpp"

task_orchestrator::SimClock clock;
clock.schedule_at(10, [](task_orchestrator::SimClock::Time t) {
  // run at t == 10
});
clock.schedule_at(5, [](auto t) { /* t == 5 */ });
clock.advance_to_next();  // runs event at 5
clock.advance_to_next();  // runs event at 10
clock.run_until(100);     // process all events with at <= 100
```

Use DES in tests to simulate time and events (e.g. task requests at t=0, phase completions at t=5) without real time. See `tests/unit/sim_clock_test.cpp` and `application/tests/des_runner_test.cpp`.

---

## Optimization and capacity

- **Algorithm**: The scheduler uses an **Earliest-Deadline-First (EDF)** style ordering: tasks are sorted by deadline (and priority); then a greedy assignment assigns each to an actor with free capacity and availability. This maximizes the number of tasks fulfilled under capacity and time-window constraints.
- **Capacity issue**: If not all tasks can be assigned (insufficient capacity or availability), `RunResult::capacity_issue` is set and `unfulfilled_task_ids` lists the task IDs that could not be fulfilled.
- **Productivity**: The runner optimizes for **fulfilling as many tasks as possible**; when not all can be fulfilled, it returns the list of unfulfilled tasks so you can adjust resources or deadlines.
