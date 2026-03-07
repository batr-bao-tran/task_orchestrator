#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__ORCHESTRATOR_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__ORCHESTRATOR_HPP_
#include <memory>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/planner_fsm.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/strategy/scheduling_strategy.hpp"

namespace task_orchestrator {

class Orchestrator {
 public:
  Orchestrator();
  ~Orchestrator();

  Orchestrator(const Orchestrator&) = delete;
  Orchestrator& operator=(const Orchestrator&) = delete;

  void set_workflow(Workflow w);
  void register_actor(Actor a);
  /** Set scheduling strategy (default: earliest-deadline-first). Takes ownership. */
  void set_scheduling_strategy(std::unique_ptr<SchedulingStrategy> strategy);
  const Workflow* workflow() const;
  const ActorRegistry* actor_registry() const;

  void start();
  void tick(Time now);
  /** Mark a phase as complete; call when all tasks in that phase are done. */
  void complete_phase(const PhaseId& phase_id);

  PlannerState current_planner_state() const;
  ScheduleResult get_latest_schedule() const;
  const WorkflowState* workflow_state() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace task_orchestrator

#endif
