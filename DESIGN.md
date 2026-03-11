# Task Orchestrator – Design and Architecture

## 1. Overview

The **task_orchestrator** is a C++ library for generic workflow coordination among multiple actors. It supports:

- **Workflows**: DAG of phases with processes and sub-processes.
- **Actors**: Resources with capacity and availability (uptime windows).
- **Scheduling**: Optimization-driven assignment of work to actors to maximize throughput and uptime utilisation.
- **Low-latency planning**: A Boost.MSM state machine drives the planning loop and phase transitions.

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
│         │  (capacity,     │  │  (task->actor,  │                │
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
- **Algorithm**: Greedy with deterministic ordering and two-stage ranking.
  - Stage 1 (task ordering): use the configured task strategy (`EDF` default; also `FIFO`, `SJF`, `PriorityOnly`).
  - Stage 2 (actor selection): evaluate only feasible actors, then apply an ordered actor ranking profile.
- **Actor ranking profile**:
  - A profile is an ordered list of criteria from most important to least important.
  - Example profile: `distance_to_work` > `earliest_feasible_start` > `uptime_utilisation`.
  - Rank comparison is lexicographic: the first criterion that differs decides; each criterion must define sort direction and deterministic tie-breakers.
- **Feasibility gate**:
  - A candidate actor is considered only if capacity and availability constraints are satisfied for the task duration.
  - Ranking never overrides feasibility.
- **Interface**: `ScheduleResult plan(const WorkflowState&, const ActorRegistry&, Time now)`.

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
- **Purpose**: Keep planning and dispatching separate from “running” so that the planner can run in a tight loop; the FSM reacts to events (e.g. PhaseComplete) and triggers the next planning round.

### 3.7 Orchestrator (Facade)

- Holds: one Workflow, one ActorRegistry, one Scheduler, one PlannerStateMachine, and current Assignment and WorkflowState.
- **API**:
  - `void set_workflow(Workflow)` and `void register_actor(Actor)`.
  - `void start()` (start FSM; move to Planning).
  - `void tick(Time)` (feed Tick or advance time; may trigger planning).
  - `ScheduleResult get_latest_schedule()` (after ScheduleReady).
  - Callbacks or observers for PhaseComplete / AllPhasesComplete can be plugged in.
- The orchestrator advances the FSM and invokes the scheduler when in Planning state; when the scheduler returns, it emits ScheduleReady and transitions to Dispatching.
- **Dynamic replanning**:
  - Replan is event-driven and idempotent: multiple triggers at the same time collapse into one planning cycle.
  - Replan triggers:
    - Actor state transition to `UNAVAILABLE` for new work.
    - Task execution failure where the task is `RESUMABLE` by another actor.
    - Task failure while `UNRESUMABLE`; planner is notified only when task status later changes to `RESUMABLE`.
  - Replan scope:
    - Keep in-flight assignments that are still valid.
    - Recompute assignments for unstarted tasks and resumable failed tasks.
  - Replan correctness requirements:
    - Never assign new work to `UNAVAILABLE` actors.
    - Do not reschedule `UNRESUMABLE` tasks.
    - When resumability becomes true, include the task in the next planning round.

## 4. Build and Dependencies

- **Build**: Bazel. C++20.
- **Dependencies**: Boost (for Boost.MSM and any used Boost headers: MPL, Fusion, etc.). No other external runtime dependencies.
- **Layout**:
  - `task_orchestrator/`: main library (public headers under `include/`, sources under `src/`).
  - `tests/unit/`: unit tests per component (workflow, phase, actor, scheduler, state machine, orchestrator, etc.).
  - `tests/scenario/`: multi-actor workflows, sub-processes, phase ordering, scheduling under constraints, FSM transitions.

## 5. Testing Strategy

The repository uses layered coverage and each new feature should add tests at the lowest useful layer first, then at one integration layer above it.

- **Current suite baseline**:
  - `task_orchestrator/tests/unit/`: data model and core logic components.
  - `task_orchestrator/tests/scenario/`: end-to-end scheduler/orchestrator behaviour.
  - `task_orchestrator/tests/scenario/strategy/`: strategy-specific ordering checks (`edf`, `fifo`, `sjf`, `priority_only`).
  - `application/tests/`: config loader, runner behavior, and DES-driven runner tests.
  - `utils/tests/`: utility behavior.
- **Developer coverage policy**:
  - Add or update unit tests for every new branch in pure logic code.
  - Add scenario tests whenever a change affects interactions between workflow, scheduler, actors, and FSM transitions.
  - Add discrete event simulation tests when correctness depends on event timing or event ordering.
  - Prefer deterministic assertions: explicit actor/task IDs, start times, and state transitions.
  - Treat strategy and ranking behaviour as contract tests: test ordering and fallback behaviour.

## 6. File Layout (Bazel)

```
application/
  BUILD
  config/
    ...
  runner/
    ...
  tests/
    ...
task_orchestrator/
  BUILD
  include/task_orchestrator/
    ...
  src/
    ...
  tests/
    unit/
      ...
    scenario/
      ...
third_party/
  BUILD (3rd party dependencies)
utils/
  BUILD
  include/utils/
    ...
  src/
    ...
  tests/
    ...
```
