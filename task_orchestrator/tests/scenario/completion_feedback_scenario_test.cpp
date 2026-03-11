#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

void setup_single_actor_two_task_orchestrator(to::Orchestrator& o) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase1", .name = "Phase 1", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "P1",
                                   .phase_id = "phase1",
                                   .sub_process_ids = {},
                                   .estimated_duration = 10,
                                   .priority = 10,
                                   .deadline = {}});
  workflow.add_process(to::Process{
      .id = "P2", .phase_id = "phase1", .sub_process_ids = {}, .estimated_duration = 4, .priority = 1, .deadline = {}});
  o.set_workflow(std::move(workflow));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.start();
  o.tick(0);
  o.tick(1);  // dispatch first assignment
}

TEST(CompletionFeedbackScenarioTest, EarlyCompletionPullsNextPlanForward) {
  to::Orchestrator o;
  setup_single_actor_two_task_orchestrator(o);
  const auto* initial_state = o.workflow_state();
  ASSERT_NE(nullptr, initial_state);
  ASSERT_TRUE(initial_state->task_planned_end_time.contains("P1"));
  const to::Time original_expected_release = initial_state->task_planned_end_time.at("P1");
  EXPECT_EQ(10, original_expected_release);

  o.notify_task_completed("P1", 3);
  o.tick(3);
  const auto replanned = o.get_latest_schedule();

  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("P2", replanned.assignments[0].task_id);
  EXPECT_EQ("A1", replanned.assignments[0].actor_id);
  EXPECT_LT(replanned.assignments[0].start_time, original_expected_release);
  EXPECT_EQ(3, replanned.assignments[0].start_time);
}

TEST(CompletionFeedbackScenarioTest, LateCompletionPushesNextPlanOutButKeepsAvailabilityValid) {
  to::Orchestrator o;
  setup_single_actor_two_task_orchestrator(o);
  const auto* initial_state = o.workflow_state();
  ASSERT_NE(nullptr, initial_state);
  ASSERT_TRUE(initial_state->task_planned_end_time.contains("P1"));
  const to::Time original_expected_release = initial_state->task_planned_end_time.at("P1");
  EXPECT_EQ(10, original_expected_release);

  o.notify_task_completed("P1", 18);
  o.tick(18);
  const auto replanned = o.get_latest_schedule();

  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("P2", replanned.assignments[0].task_id);
  EXPECT_EQ("A1", replanned.assignments[0].actor_id);
  EXPECT_GT(replanned.assignments[0].start_time, original_expected_release);
  EXPECT_EQ(18, replanned.assignments[0].start_time);
  EXPECT_LE(replanned.assignments[0].start_time + 4, 100);
}

TEST(CompletionFeedbackScenarioTest, ReplannedStartTimeChangesToMaintainProductivityWithinAvailabilityWindows) {
  to::Orchestrator o;
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase1", .name = "Phase 1", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "P1",
                                   .phase_id = "phase1",
                                   .sub_process_ids = {},
                                   .estimated_duration = 10,
                                   .priority = 10,
                                   .deadline = {}});
  workflow.add_process(to::Process{
      .id = "P2", .phase_id = "phase1", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  o.set_workflow(std::move(workflow));
  o.register_actor(to::Actor{.id = "A1",
                             .capacity = 1,
                             .availability_windows = {{.start = 0, .end = 12}, {.start = 20, .end = 40}},
                             .current_load = 0});

  o.start();
  o.tick(0);
  o.tick(1);  // dispatch P1

  const auto* initial_state = o.workflow_state();
  ASSERT_NE(nullptr, initial_state);
  const to::Time original_expected_release = initial_state->task_planned_end_time.at("P1");
  EXPECT_EQ(10, original_expected_release);

  // Runtime is later than planned and misses the first useful continuation window.
  o.notify_task_completed("P1", 21);
  o.tick(21);
  const auto replanned = o.get_latest_schedule();

  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("P2", replanned.assignments[0].task_id);
  EXPECT_NE(replanned.assignments[0].start_time, original_expected_release);
  EXPECT_GE(replanned.assignments[0].start_time, 20);
  EXPECT_LE(replanned.assignments[0].start_time + 5, 40);
}
}  // namespace
