#include "task_orchestrator/core/planner_fsm.hpp"

#include <gtest/gtest.h>

namespace {
namespace to = task_orchestrator;

TEST(PlannerFsmTest, InitialState) {
  to::PlannerStateMachine fsm;
  EXPECT_EQ(to::PlannerState::Idle, fsm.current_state());
  EXPECT_EQ("Idle", fsm.current_state_name());
}

TEST(PlannerFsmTest, StartPlanning) {
  to::PlannerStateMachine fsm;
  fsm.process_event(to::StartPlanning{});
  EXPECT_EQ(to::PlannerState::Planning, fsm.current_state());
}

TEST(PlannerFsmTest, FullTransitionSequence) {
  to::PlannerStateMachine fsm2;
  fsm2.process_event(to::StartPlanning{});
  EXPECT_EQ(to::PlannerState::Planning, fsm2.current_state());
  fsm2.process_event(to::ScheduleReady{});
  EXPECT_EQ(to::PlannerState::Dispatching, fsm2.current_state());
  fsm2.process_event(to::DispatchComplete{});
  EXPECT_EQ(to::PlannerState::Running, fsm2.current_state());
  fsm2.process_event(to::PhaseComplete{});
  EXPECT_EQ(to::PlannerState::Running, fsm2.current_state());
  fsm2.process_event(to::AllPhasesComplete{});
  EXPECT_EQ(to::PlannerState::Idle, fsm2.current_state());
}

TEST(PlannerFsmTest, StateChangeCallback) {
  to::PlannerStateMachine fsm3;
  to::PlannerState from = to::PlannerState::Idle;
  to::PlannerState to = to::PlannerState::Idle;
  fsm3.set_state_change_callback([&](to::PlannerState s, to::PlannerState prev) {
    to = s;
    from = prev;
  });
  fsm3.process_event(to::StartPlanning{});
  EXPECT_EQ(to::PlannerState::Planning, to);
  EXPECT_EQ(to::PlannerState::Idle, from);
}
}  // namespace
