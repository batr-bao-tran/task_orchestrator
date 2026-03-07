#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__PLANNER_FSM_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__PLANNER_FSM_HPP_
#include <functional>
#include <ostream>
#include <string>

namespace task_orchestrator {

/** Planner FSM state IDs (Boost.MSM back-end agnostic). */
enum class PlannerState {
  Idle,
  Planning,
  Dispatching,
  Running,
  Completing,
};

inline std::ostream& operator<<(std::ostream& os, PlannerState s) {
  switch (s) {
    case PlannerState::Idle:
      return os << "Idle";
    case PlannerState::Planning:
      return os << "Planning";
    case PlannerState::Dispatching:
      return os << "Dispatching";
    case PlannerState::Running:
      return os << "Running";
    case PlannerState::Completing:
      return os << "Completing";
    default:
      return os << "PlannerState(?)";
  }
}

/** Events that drive the planner FSM. */
struct StartPlanning {};
struct ScheduleReady {};
struct DispatchComplete {};
struct PhaseComplete {};
struct AllPhasesComplete {};
struct Tick {};

/** Callback when state changes (state_id, previous_state_id). */
using StateChangeCallback = std::function<void(PlannerState, PlannerState)>;

/** Lightweight wrapper around Boost.MSM for the planner FSM. */
class PlannerStateMachine {
 public:
  PlannerStateMachine();
  ~PlannerStateMachine();

  PlannerStateMachine(const PlannerStateMachine&) = delete;
  PlannerStateMachine& operator=(const PlannerStateMachine&) = delete;

  PlannerState current_state() const;
  std::string current_state_name() const;

  void set_state_change_callback(StateChangeCallback cb);
  void process_event(StartPlanning);
  void process_event(ScheduleReady);
  void process_event(DispatchComplete);
  void process_event(PhaseComplete);
  void process_event(AllPhasesComplete);
  void process_event(Tick);

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace task_orchestrator

#endif
