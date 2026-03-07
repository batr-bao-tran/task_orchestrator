#include <gtest/gtest.h>

#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/planner_fsm.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(FsmLifecycleTest, StateChangeCallback) {
  to::PlannerStateMachine fsm;
  std::vector<to::PlannerState> states;
  fsm.set_state_change_callback([&](to::PlannerState s, to::PlannerState) { states.push_back(s); });

  EXPECT_EQ(to::PlannerState::Idle, fsm.current_state());
  fsm.process_event(to::StartPlanning{});
  fsm.process_event(to::ScheduleReady{});
  fsm.process_event(to::DispatchComplete{});
  fsm.process_event(to::PhaseComplete{});
  fsm.process_event(to::AllPhasesComplete{});

  ASSERT_GE(states.size(), 1U);
  EXPECT_EQ(to::PlannerState::Idle, fsm.current_state());
}

TEST(FsmLifecycleTest, OrchestratorStartAndTick) {
  to::Orchestrator o;
  to::Workflow w("wf1");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});

  o.start();
  to::PlannerState s0 = o.current_planner_state();
  EXPECT_TRUE(s0 == to::PlannerState::Planning || s0 == to::PlannerState::Dispatching);

  o.tick(0);
  o.tick(1);
  EXPECT_TRUE(o.current_planner_state() == to::PlannerState::Running ||
              o.current_planner_state() == to::PlannerState::Planning ||
              o.current_planner_state() == to::PlannerState::Dispatching);
}
}  // namespace
