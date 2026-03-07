#include "task_orchestrator/core/planner_fsm.hpp"

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

namespace task_orchestrator {

namespace msm = boost::msm;
namespace mpl = boost::mpl;

namespace {

struct Idle : msm::front::state<> {};
struct Planning : msm::front::state<> {};
struct Dispatching : msm::front::state<> {};
struct Running : msm::front::state<> {};

struct planner_fsm_def : msm::front::state_machine_def<planner_fsm_def> {
  using initial_state = Idle;

  typedef mpl::vector<msm::front::Row<Idle, StartPlanning, Planning, msm::front::none, msm::front::none>,
                      msm::front::Row<Planning, ScheduleReady, Dispatching, msm::front::none, msm::front::none>,
                      msm::front::Row<Dispatching, DispatchComplete, Running, msm::front::none, msm::front::none>,
                      msm::front::Row<Running, PhaseComplete, Running, msm::front::none, msm::front::none>,
                      msm::front::Row<Running, AllPhasesComplete, Idle, msm::front::none, msm::front::none>,
                      msm::front::Row<Planning, PhaseComplete, Running, msm::front::none, msm::front::none>,
                      msm::front::Row<Dispatching, PhaseComplete, Running, msm::front::none, msm::front::none>,
                      msm::front::Row<Planning, Tick, Planning, msm::front::none, msm::front::none>,
                      msm::front::Row<Idle, Tick, Idle, msm::front::none, msm::front::none>,
                      msm::front::Row<Running, Tick, Running, msm::front::none, msm::front::none>>
      transition_table;
};

typedef msm::back::state_machine<planner_fsm_def> planner_fsm_backend;

}  // namespace

struct PlannerStateMachine::Impl {
  planner_fsm_backend fsm;
  StateChangeCallback callback;
  PlannerState last_known_state = PlannerState::Idle;

  void update_state(PlannerState next) {
    if (next != last_known_state) {
      if (callback) {
        callback(next, last_known_state);
      }
      last_known_state = next;
    }
  }
};

PlannerStateMachine::PlannerStateMachine() : impl_(new Impl) {}

PlannerStateMachine::~PlannerStateMachine() { delete impl_; }

PlannerState PlannerStateMachine::current_state() const { return impl_->last_known_state; }

std::string PlannerStateMachine::current_state_name() const {
  switch (impl_->last_known_state) {
    case PlannerState::Idle:
      return "Idle";
    case PlannerState::Planning:
      return "Planning";
    case PlannerState::Dispatching:
      return "Dispatching";
    case PlannerState::Running:
      return "Running";
    case PlannerState::Completing:
      return "Completing";
    default:
      return "Unknown";
  }
}

void PlannerStateMachine::set_state_change_callback(StateChangeCallback cb) { impl_->callback = std::move(cb); }

void PlannerStateMachine::process_event(StartPlanning) {
  impl_->fsm.process_event(StartPlanning{});
  impl_->update_state(PlannerState::Planning);
}

void PlannerStateMachine::process_event(ScheduleReady) {
  impl_->fsm.process_event(ScheduleReady{});
  impl_->update_state(PlannerState::Dispatching);
}

void PlannerStateMachine::process_event(DispatchComplete) {
  impl_->fsm.process_event(DispatchComplete{});
  impl_->update_state(PlannerState::Running);
}

void PlannerStateMachine::process_event(PhaseComplete) {
  impl_->fsm.process_event(PhaseComplete{});
  impl_->update_state(PlannerState::Running);
}

void PlannerStateMachine::process_event(AllPhasesComplete) {
  impl_->fsm.process_event(AllPhasesComplete{});
  impl_->update_state(PlannerState::Idle);
}

void PlannerStateMachine::process_event(Tick) { impl_->fsm.process_event(Tick{}); }

}  // namespace task_orchestrator
