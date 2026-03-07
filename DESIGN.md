# Task Orchestrator – Design and Architecture

## 1. Overview

The **task_orchestrator** is a C++ library for generic workflow coordination among multiple actors. It supports:

- **Workflows**: DAG of phases with processes and sub-processes.
- **Actors**: Resources with capacity and availability (uptime windows).
- **Scheduling**: Optimization-driven assignment of work to actors to maximize throughput and uptime utilisation.
- **Low-latency planning**: A Boost.MSM state machine drives the planning loop and phase transitions for fast reaction.

## 2. Design Goals

- **Generic**: Workflows, phases, and tasks are not tied to a specific domain.
- **Throughput**: Scheduler maximizes completed work per time unit.
- **Uptime**: Assignments respect actor availability and prefer high utilisation.
- **Low latency**: State machine and explicit planning phase keep decision latency small.
- **Testability**: Every component is unit-testable; scenario tests cover end-to-end behaviour.

## 3. Architecture

### 3.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     TaskOrchestrator (facade)                   │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐   │
│  │  Workflow    │  │   Scheduler  │  │  PlannerStateMachine │   │
│  │  (DAG)       │  │ (optimiser)  │  │  (Boost.MSM)         │   │
│  └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘   │
│         │                 │                     │               │
│         └────────┬────────┴─────────────────────┘               │
│                  │                                              │
│         ┌────────▼────────┐  ┌─────────────────┐                │
│         │  ActorRegistry  │  │  Assignment     │                │
│         │  (capacity,     │  │  (task→actor,   │                │
│         │   availability) │  │   start time)   │                │
│         └─────────────────┘  └─────────────────┘                │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 Core Abstractions

| Concept | Description |
|--------|--------------|
| **Workflow** | DAG of phases; each phase has a set of tasks (processes). Sub-processes are either child phases or nested workflow references. |
| **Phase** | A node in the workflow DAG. Has in-edges (dependencies) and out-edges. Contains one or more **Process**es. |
| **Process** | Unit of work; may contain **SubProcess**es (hierarchical). Has estimated duration, priority, and optional deadline. |
| **Task** | Schedulable unit: either a Process or a SubProcess. Identified by TaskId. |
| **Actor** | Resource that can execute tasks. Has ActorId, capacity (e.g. slots or throughput), and **AvailabilityWindow**s (uptime). |
| **Assignment** | Mapping from TaskId → (ActorId, start_time). Produced by the scheduler. |
| **PlannerStateMachine** | Boost.MSM FSM: Idle → Planning → Dispatching → Running → (back to Idle or Planning). Events: StartPlanning, ScheduleReady, DispatchComplete, PhaseComplete, etc. |

### 3.3 Workflow Model

- **WorkflowId**: unique identifier.
- **Phase**: `phase_id`, `name`, `process_ids[]`, `dependency_phase_ids[]` (predecessors in DAG).
- **Process**: `process_id`, `phase_id`, `sub_process_ids[]`, `estimated_duration`, `priority`, optional `deadline`.
- Execution semantics: a phase is *ready* when all dependency phases are complete; within a phase, processes/sub-processes are scheduled according to the optimisation (no internal DAG among processes in the same phase for simplicity; they can be scheduled in parallel up to actor capacity).

### 3.4 Actor and Availability

- **Actor**: `actor_id`, `capacity` (e.g. number of concurrent tasks or “units” of work), `availability_windows`: list of `[start_time, end_time]` (uptime).
- **Current load**: number (or weight) of tasks currently assigned and not yet completed.
- Scheduler only assigns a task to an actor if: (1) the actor has free capacity at the chosen start time, and (2) the interval `[start_time, start_time + duration]` lies within one of the actor’s availability windows.

### 3.5 Scheduler (Optimisation)

- **Input**: Current workflow state (which phases are done, which tasks are pending), actor registry (capacities, availability, current load), and optionally current time.
- **Output**: Assignment set (task → actor, start_time).
- **Objective**: Maximise throughput (e.g. total work completed per unit time) and uptime utilisation (prefer using available actor time).
- **Algorithm**: Greedy with priority and capacity constraints (low latency, deterministic). Optionally extend with a simple LP or greedy bin-packing for batching.
  - Sort pending tasks by priority (and optionally deadline).
  - For each task, choose an actor and start time that: (a) satisfy capacity and availability, (b) minimise start time (earliest feasible), (c) maximise utilisation (prefer actors with more remaining availability).
- **Interface**: `ScheduleResult plan(WorkflowState const&, ActorRegistry const&, Time now)`.

### 3.6 Boost.MSM State Machine (Planner FSM)

- **States**: Idle, Planning, Dispatching, Running, Completing.
- **Events**: StartPlanning, ScheduleReady, DispatchComplete, PhaseComplete, AllPhasesComplete, Tick.
- **Transitions** (summary):
  - Idle + StartPlanning → Planning.
  - Planning + ScheduleReady → Dispatching (schedule available).
  - Dispatching + DispatchComplete → Running (assignments applied).
  - Running + PhaseComplete → Running (update state) or Completing if phase was last.
  - Running + AllPhasesComplete → Idle.
  - Completing + (cleanup) → Idle.
- **Purpose**: Keep planning and dispatching separate from “running” so that the planner can run in a tight loop with low latency; the FSM reacts to events (e.g. PhaseComplete) and triggers the next planning round.

### 3.7 Orchestrator (Facade)

- Holds: one Workflow, one ActorRegistry, one Scheduler, one PlannerStateMachine, and current Assignment and WorkflowState.
- **API** (high level):
  - `void set_workflow(Workflow)` and `void register_actor(Actor)`.
  - `void start()` (start FSM; move to Planning).
  - `void tick(Time)` (feed Tick or advance time; may trigger planning).
  - `ScheduleResult get_latest_schedule()` (after ScheduleReady).
  - Callbacks or observers for PhaseComplete / AllPhasesComplete can be plugged in.
- The orchestrator advances the FSM and invokes the scheduler when in Planning state; when the scheduler returns, it emits ScheduleReady and transitions to Dispatching.

## 4. Build and Dependencies

- **Build**: Bazel. C++17.
- **Dependencies**: Boost (for Boost.MSM and any used Boost headers: MPL, Fusion, etc.). No other external runtime dependencies.
- **Layout**:
  - `task_orchestrator/`: main library (public headers under `include/`, sources under `src/`).
  - `tests/unit/`: unit tests per component (workflow, phase, actor, scheduler, state machine, orchestrator).
  - `tests/scenario/`: multi-actor workflows, sub-processes, phase ordering, scheduling under constraints, FSM transitions.

## 5. Testing Strategy

- **Unit tests** (per component):
  - Workflow: build DAG, query phases, dependencies, ready phases.
  - Phase: add/remove processes, dependency checks.
  - Actor: capacity, availability windows, load.
  - Scheduler: given a small WorkflowState and ActorRegistry, assert assignment satisfies capacity and availability and respects priorities.
  - PlannerStateMachine: inject events, assert state transitions and possibly entry/exit actions.
  - Orchestrator: mock or minimal workflow/actors, start/tick, assert FSM and schedule output.
- **Scenario tests**:
  - Two-phase workflow, two actors with different availability; assert assignments and phase order.
  - Sub-processes: one phase with one process containing two sub-processes; assert both sub-processes scheduled.
  - Throughput: multiple tasks, limited actors; assert no over-assignment and that high-priority tasks get earlier slots.
  - Uptime: actors with gaps; assert no assignment in gaps.
  - FSM: full cycle Idle → Planning → Dispatching → Running → PhaseComplete → … → Idle.

## 6. File Layout (Bazel)

```
task_orchestrator/
  BUILD
  include/task_orchestrator/
    types.hpp
    workflow.hpp
    phase.hpp
    process.hpp
    actor.hpp
    scheduler.hpp
    planner_fsm.hpp
    orchestrator.hpp
  src/
    workflow.cpp
    phase.cpp
    process.cpp
    actor.cpp
    scheduler.cpp
    planner_fsm.cpp
    orchestrator.cpp
third_party/
  BUILD (or boost.BUILD for Boost)
tests/
  BUILD
  unit/
    BUILD
    workflow_test.cpp
    phase_test.cpp
    process_test.cpp
    actor_test.cpp
    scheduler_test.cpp
    planner_fsm_test.cpp
    orchestrator_test.cpp
  scenario/
    BUILD
    multi_actor_workflow_test.cpp
    subprocess_workflow_test.cpp
    throughput_scheduling_test.cpp
    uptime_availability_test.cpp
    fsm_lifecycle_test.cpp
```

## 7. Chosen Approach Summary

- **State machine**: Boost.MSM for a clear, explicit FSM and low-latency planning loop.
- **Scheduling**: Greedy priority- and capacity-aware algorithm first (simple, predictable, fast); structure allows swapping in an LP or other optimiser later.
- **Workflow**: Explicit DAG of phases; processes and sub-processes live in phases with clear ownership.
- **Actors**: Registry with capacity and time-window availability; scheduler queries it read-only.
- **Testing**: Full unit coverage per type and component; scenario tests for integration, throughput, uptime, and FSM lifecycle.

This design keeps the library generic, testable, and extensible while meeting the requirements for multi-actor coordination, processes/sub-processes/phases, throughput/uptime-oriented scheduling, and low-latency planning via Boost.MSM.
