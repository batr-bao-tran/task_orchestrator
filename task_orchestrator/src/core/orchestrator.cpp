#include "task_orchestrator/core/orchestrator.hpp"

#include <algorithm>
#include <unordered_set>

#include "task_orchestrator/core/planner_fsm.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/strategy/edf_strategy.hpp"

namespace task_orchestrator {

struct Orchestrator::Impl {
  Workflow workflow_;
  ActorRegistry registry_;
  Scheduler scheduler_;
  std::unique_ptr<SchedulingStrategy> strategy_;
  PlannerStateMachine fsm_;
  WorkflowState workflow_state_;
  ScheduleResult latest_schedule_;
  bool started_ = false;

  Impl() : strategy_(std::make_unique<EDFStrategy>()) {
    fsm_.set_state_change_callback([](PlannerState, PlannerState) { /* optional: log */ });
  }

  void run_planning(Time now) {
    latest_schedule_ = Scheduler::plan(workflow_, workflow_state_, registry_, now, strategy_.get());
    if (latest_schedule_.ok && !latest_schedule_.assignments.empty()) {
      fsm_.process_event(ScheduleReady{});
    }
  }

  void apply_dispatch() {
    for (const Assignment& assignment : latest_schedule_.assignments) {
      workflow_state_.assigned_tasks.push_back(assignment.task_id);
      Actor* actor = registry_.get_mutable(assignment.actor_id);
      if (actor) {
        actor->current_load++;
      }
      workflow_state_.actor_load[assignment.actor_id] =
          registry_.get(assignment.actor_id) ? registry_.get(assignment.actor_id)->current_load : 0U;
    }
    fsm_.process_event(DispatchComplete{});
  }
};

Orchestrator::Orchestrator() : impl_(new Impl) {}

Orchestrator::~Orchestrator() = default;

void Orchestrator::set_workflow(Workflow w) {
  impl_->workflow_ = std::move(w);
  impl_->workflow_state_ = WorkflowState{};
}

void Orchestrator::register_actor(Actor a) { impl_->registry_.add(std::move(a)); }

void Orchestrator::set_scheduling_strategy(std::unique_ptr<SchedulingStrategy> strategy) {
  if (strategy) {
    impl_->strategy_ = std::move(strategy);
  }
}

const Workflow* Orchestrator::workflow() const { return &impl_->workflow_; }

const ActorRegistry* Orchestrator::actor_registry() const { return &impl_->registry_; }

void Orchestrator::start() {
  impl_->started_ = true;
  impl_->fsm_.process_event(StartPlanning{});
  impl_->run_planning(0);
}

void Orchestrator::tick(Time now) {
  if (!impl_->started_) {
    return;
  }
  PlannerState s = impl_->fsm_.current_state();
  if (s == PlannerState::Idle) {
    impl_->fsm_.process_event(StartPlanning{});
    impl_->run_planning(now);
  } else if (s == PlannerState::Planning) {
    impl_->run_planning(now);
  } else if (s == PlannerState::Dispatching) {
    impl_->apply_dispatch();
  } else if (s == PlannerState::Running) {
    impl_->fsm_.process_event(Tick{});
  }
}

void Orchestrator::complete_phase(const PhaseId& phase_id) {
  auto& completed = impl_->workflow_state_.completed_phases;
  if (std::ranges::find(completed, phase_id) == completed.end()) {
    completed.push_back(phase_id);
  }
  impl_->fsm_.process_event(PhaseComplete{});
  const std::vector<PhaseId> all = impl_->workflow_.phase_ids();
  if (completed.size() >= all.size()) {
    impl_->fsm_.process_event(AllPhasesComplete{});
  }
}

PlannerState Orchestrator::current_planner_state() const { return impl_->fsm_.current_state(); }

ScheduleResult Orchestrator::get_latest_schedule() const { return impl_->latest_schedule_; }

const WorkflowState* Orchestrator::workflow_state() const { return &impl_->workflow_state_; }

}  // namespace task_orchestrator
