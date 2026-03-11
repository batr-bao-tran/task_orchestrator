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
  ActorRankingProfile actor_ranking_profile_;
  PlannerStateMachine fsm_;
  WorkflowState workflow_state_;
  ScheduleResult latest_schedule_;
  bool started_ = false;
  bool replan_requested_ = false;

  Impl() : strategy_(std::make_unique<EDFStrategy>()) {
    fsm_.set_state_change_callback([](PlannerState, PlannerState) { /* optional: log */ });
  }

  void run_planning(Time now) {
    latest_schedule_ =
        Scheduler::plan(workflow_, workflow_state_, registry_, now, strategy_.get(), &actor_ranking_profile_);
    if (latest_schedule_.ok && !latest_schedule_.assignments.empty()) {
      fsm_.process_event(ScheduleReady{});
    }
  }

  void request_replan() { replan_requested_ = true; }

  static bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::ranges::find(values, value) != values.end();
  }

  static void erase_value(std::vector<std::string>& values, const std::string& value) {
    auto [first, last] = std::ranges::remove(values, value);
    values.erase(first, last);
  }

  Duration task_duration(const TaskId& task_id) const {
    const Process* proc = workflow_.process_for_task(task_id);
    return proc ? proc->estimated_duration : 1;
  }

  void apply_dispatch() {
    for (const Assignment& assignment : latest_schedule_.assignments) {
      if (!contains(workflow_state_.assigned_tasks, assignment.task_id)) {
        workflow_state_.assigned_tasks.push_back(assignment.task_id);
      }
      erase_value(workflow_state_.resumable_tasks, assignment.task_id);
      erase_value(workflow_state_.unresumable_tasks, assignment.task_id);
      workflow_state_.task_actor[assignment.task_id] = assignment.actor_id;
      Actor* actor = registry_.get_mutable(assignment.actor_id);
      if (actor) {
        actor->current_load++;
      }
      workflow_state_.actor_load[assignment.actor_id] =
          registry_.get(assignment.actor_id) ? registry_.get(assignment.actor_id)->current_load : 0U;
      workflow_state_.task_planned_start_time[assignment.task_id] = assignment.start_time;
      workflow_state_.task_planned_end_time[assignment.task_id] =
          assignment.start_time + task_duration(assignment.task_id);
    }
    fsm_.process_event(DispatchComplete{});
  }
};

Orchestrator::Orchestrator() : impl_(new Impl) {}

Orchestrator::~Orchestrator() noexcept = default;

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

void Orchestrator::set_actor_ranking_profile(ActorRankingProfile profile) {
  impl_->actor_ranking_profile_ = std::move(profile);
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
  if (impl_->replan_requested_ && s != PlannerState::Planning) {
    impl_->fsm_.process_event(StartPlanning{});
    impl_->replan_requested_ = false;
    impl_->run_planning(now);
    return;
  }
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

void Orchestrator::set_actor_unavailable(const ActorId& actor_id, bool unavailable) {
  auto& unavailable_actors = impl_->workflow_state_.unavailable_actors;
  const auto it = std::ranges::find(unavailable_actors, actor_id);
  if (unavailable) {
    if (it == unavailable_actors.end()) {
      unavailable_actors.push_back(actor_id);
      impl_->request_replan();
    }
  } else if (it != unavailable_actors.end()) {
    unavailable_actors.erase(it);
    impl_->request_replan();
  }
}

void Orchestrator::notify_task_failed(const TaskId& task_id, bool resumable) {
  auto& state = impl_->workflow_state_;
  Impl::erase_value(state.assigned_tasks, task_id);
  Impl::erase_value(state.completed_tasks, task_id);
  const auto owner_it = state.task_actor.find(task_id);
  if (owner_it != state.task_actor.end()) {
    Actor* owner = impl_->registry_.get_mutable(owner_it->second);
    if (owner && owner->current_load > 0) {
      owner->current_load--;
      state.actor_load[owner->id] = owner->current_load;
    }
    state.task_actor.erase(owner_it);
  }

  Impl::erase_value(state.unresumable_tasks, task_id);
  Impl::erase_value(state.resumable_tasks, task_id);
  if (resumable) {
    state.resumable_tasks.push_back(task_id);
    impl_->request_replan();
  } else {
    state.unresumable_tasks.push_back(task_id);
  }
}

void Orchestrator::notify_task_completed(const TaskId& task_id, Time actual_completion_time) {
  auto& state = impl_->workflow_state_;
  Impl::erase_value(state.assigned_tasks, task_id);
  if (std::ranges::find(state.completed_tasks, task_id) == state.completed_tasks.end()) {
    state.completed_tasks.push_back(task_id);
  }

  const auto owner_it = state.task_actor.find(task_id);
  if (owner_it != state.task_actor.end()) {
    Actor* owner = impl_->registry_.get_mutable(owner_it->second);
    if (owner && owner->current_load > 0) {
      owner->current_load--;
      state.actor_load[owner->id] = owner->current_load;
    }
    state.task_actor.erase(owner_it);
  }
  state.task_actual_completion_time[task_id] = actual_completion_time;
  impl_->request_replan();
}

void Orchestrator::mark_task_resumable(const TaskId& task_id) {
  auto& state = impl_->workflow_state_;
  Impl::erase_value(state.unresumable_tasks, task_id);
  if (std::ranges::find(state.resumable_tasks, task_id) == state.resumable_tasks.end()) {
    state.resumable_tasks.push_back(task_id);
  }
  impl_->request_replan();
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
