#include "task_orchestrator/core/planner_fsm.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <sstream>

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

TEST(PlannerFsmTest, StartPlanningFromRunningTransitionsToPlanning) {
  to::PlannerStateMachine fsm;
  fsm.process_event(to::StartPlanning{});
  fsm.process_event(to::ScheduleReady{});
  fsm.process_event(to::DispatchComplete{});
  ASSERT_EQ(to::PlannerState::Running, fsm.current_state());

  fsm.process_event(to::StartPlanning{});
  EXPECT_EQ(to::PlannerState::Planning, fsm.current_state());
}

TEST(PlannerFsmTest, StateNamesCoverPlanningDispatchingAndRunning) {
  to::PlannerStateMachine fsm;
  fsm.process_event(to::StartPlanning{});
  EXPECT_EQ("Planning", fsm.current_state_name());

  fsm.process_event(to::ScheduleReady{});
  EXPECT_EQ("Dispatching", fsm.current_state_name());

  fsm.process_event(to::DispatchComplete{});
  EXPECT_EQ("Running", fsm.current_state_name());
}

struct TickCase {
  std::string name;
  std::function<void(to::PlannerStateMachine&)> arrange;
  to::PlannerState expected_state = to::PlannerState::Idle;
};

class PlannerFsmTickParamTest : public ::testing::TestWithParam<TickCase> {};

TEST_P(PlannerFsmTickParamTest, TickPreservesState) {
  const TickCase& test_case = GetParam();
  to::PlannerStateMachine fsm;
  test_case.arrange(fsm);

  fsm.process_event(to::Tick{});
  EXPECT_EQ(test_case.expected_state, fsm.current_state());
}

INSTANTIATE_TEST_SUITE_P(TickStates,
                         PlannerFsmTickParamTest,
                         ::testing::Values(
                             TickCase{
                                 .name = "idle",
                                 .arrange = [](to::PlannerStateMachine&) {},
                                 .expected_state = to::PlannerState::Idle,
                             },
                             TickCase{
                                 .name = "planning",
                                 .arrange =
                                     [](to::PlannerStateMachine& fsm) { fsm.process_event(to::StartPlanning{}); },
                                 .expected_state = to::PlannerState::Planning,
                             },
                             TickCase{
                                 .name = "running",
                                 .arrange =
                                     [](to::PlannerStateMachine& fsm) {
                                       fsm.process_event(to::StartPlanning{});
                                       fsm.process_event(to::ScheduleReady{});
                                       fsm.process_event(to::DispatchComplete{});
                                     },
                                 .expected_state = to::PlannerState::Running,
                             }),
                         [](const ::testing::TestParamInfo<TickCase>& info) { return info.param.name; });

TEST(PlannerFsmTest, StreamOperatorRendersAllEnumStates) {
  std::ostringstream stream;
  stream << to::PlannerState::Idle << "," << to::PlannerState::Planning << "," << to::PlannerState::Dispatching << ","
         << to::PlannerState::Running << "," << to::PlannerState::Completing;
  EXPECT_EQ("Idle,Planning,Dispatching,Running,Completing", stream.str());

  std::ostringstream invalid_stream;
  invalid_stream << static_cast<to::PlannerState>(999);
  EXPECT_EQ("PlannerState(?)", invalid_stream.str());
}

struct InvalidTransitionCase {
  std::string name;
  std::function<void(to::PlannerStateMachine&)> arrange;
  std::function<void(to::PlannerStateMachine&)> apply_invalid_event;
  to::PlannerState expected_state = to::PlannerState::Idle;
};

class PlannerFsmInvalidTransitionParamTest : public ::testing::TestWithParam<InvalidTransitionCase> {};

TEST_P(PlannerFsmInvalidTransitionParamTest, InvalidEventsLeaveStateUnchanged) {
  const InvalidTransitionCase& test_case = GetParam();
  to::PlannerStateMachine fsm;
  test_case.arrange(fsm);

  const to::PlannerState before = fsm.current_state();
  test_case.apply_invalid_event(fsm);
  EXPECT_EQ(before, fsm.current_state());
  EXPECT_EQ(test_case.expected_state, fsm.current_state());
}

INSTANTIATE_TEST_SUITE_P(
    InvalidTransitions,
    PlannerFsmInvalidTransitionParamTest,
    ::testing::Values(
        InvalidTransitionCase{
            .name = "idle_dispatch_complete",
            .arrange = [](to::PlannerStateMachine&) {},
            .apply_invalid_event = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::DispatchComplete{}); },
            .expected_state = to::PlannerState::Idle,
        },
        InvalidTransitionCase{
            .name = "planning_dispatch_complete",
            .arrange = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::StartPlanning{}); },
            .apply_invalid_event = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::DispatchComplete{}); },
            .expected_state = to::PlannerState::Planning,
        },
        InvalidTransitionCase{
            .name = "dispatching_all_phases_complete",
            .arrange =
                [](to::PlannerStateMachine& fsm) {
                  fsm.process_event(to::StartPlanning{});
                  fsm.process_event(to::ScheduleReady{});
                },
            .apply_invalid_event = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::AllPhasesComplete{}); },
            .expected_state = to::PlannerState::Dispatching,
        },
        InvalidTransitionCase{
            .name = "idle_phase_complete",
            .arrange = [](to::PlannerStateMachine&) {},
            .apply_invalid_event = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::PhaseComplete{}); },
            .expected_state = to::PlannerState::Idle,
        },
        InvalidTransitionCase{
            .name = "idle_schedule_ready",
            .arrange = [](to::PlannerStateMachine&) {},
            .apply_invalid_event = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::ScheduleReady{}); },
            .expected_state = to::PlannerState::Idle,
        },
        InvalidTransitionCase{
            .name = "dispatching_tick",
            .arrange =
                [](to::PlannerStateMachine& fsm) {
                  fsm.process_event(to::StartPlanning{});
                  fsm.process_event(to::ScheduleReady{});
                },
            .apply_invalid_event = [](to::PlannerStateMachine& fsm) { fsm.process_event(to::Tick{}); },
            .expected_state = to::PlannerState::Dispatching,
        }),
    [](const ::testing::TestParamInfo<InvalidTransitionCase>& info) { return info.param.name; });
}  // namespace
