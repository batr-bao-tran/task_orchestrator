#include "task_orchestrator/core/orchestrator.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(OrchestratorTest, SetWorkflowAndRegisterActor) {
  to::Orchestrator o;
  to::Workflow w("wf1");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {}, .dependency_phase_ids = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  ASSERT_NE(nullptr, o.workflow());
  EXPECT_EQ("wf1", o.workflow()->id());
  ASSERT_NE(nullptr, o.actor_registry());
  EXPECT_EQ(1U, o.actor_registry()->actor_ids().size());
}

TEST(OrchestratorTest, StartAndTick) {
  to::Orchestrator o2;
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w2.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  o2.set_workflow(std::move(w2));
  o2.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  o2.start();
  EXPECT_TRUE(o2.current_planner_state() == to::PlannerState::Planning ||
              o2.current_planner_state() == to::PlannerState::Dispatching);

  o2.tick(0);
  auto sched = o2.get_latest_schedule();
  EXPECT_TRUE(sched.ok);
}

TEST(OrchestratorTest, CompletePhase) {
  to::Orchestrator o3;
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{.id = "p1", .name = "P1", .process_ids = {}, .dependency_phase_ids = {}});
  w3.add_phase(to::Phase{.id = "p2", .name = "P2", .process_ids = {}, .dependency_phase_ids = {"p1"}});
  o3.set_workflow(std::move(w3));
  o3.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  o3.start();
  o3.complete_phase("p1");
  const auto* state = o3.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_EQ(1U, state->completed_phases.size());
  EXPECT_EQ("p1", state->completed_phases[0]);
}

TEST(OrchestratorTest, UnavailableActorTriggersReplanToAlternativeActor) {
  to::Orchestrator o;
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(
      to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  o.start();
  o.tick(0);
  o.tick(1);  // dispatch initial assignment
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  o.notify_task_failed(state->assigned_tasks.front(), true);
  o.set_actor_unavailable("A1", true);

  o.tick(2);
  auto replanned = o.get_latest_schedule();
  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("A2", replanned.assignments[0].actor_id);
}

TEST(OrchestratorTest, UnresumableFailureDoesNotReplanUntilResumable) {
  to::Orchestrator o;
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(
      to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  o.start();
  o.tick(0);
  o.tick(1);  // dispatch

  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();
  o.notify_task_failed(failed_task, false);

  // No replan requested for unresumable failure; schedule remains unchanged on tick.
  const auto before = o.get_latest_schedule();
  o.tick(2);
  const auto after_unresumable = o.get_latest_schedule();
  EXPECT_EQ(before.assignments.size(), after_unresumable.assignments.size());

  o.mark_task_resumable(failed_task);
  o.tick(3);
  const auto after_resumable = o.get_latest_schedule();
  ASSERT_TRUE(after_resumable.ok);
  ASSERT_EQ(1U, after_resumable.assignments.size());
}

TEST(OrchestratorTest, DuplicateTriggersCoalesceIntoSingleReplanCycle) {
  to::Orchestrator o;
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(
      to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  o.start();
  o.tick(0);
  o.tick(1);  // dispatch

  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  o.notify_task_failed(failed_task, true);
  o.set_actor_unavailable("A1", true);
  o.set_actor_unavailable("A1", true);  // duplicate trigger

  o.tick(2);  // one planning cycle
  EXPECT_EQ(to::PlannerState::Dispatching, o.current_planner_state());
}

TEST(OrchestratorTest, EarlyCompletionTriggersReplanAndPullsScheduleForward) {
  to::Orchestrator o;
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 10, .deadline = {}});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});

  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  o.start();
  o.tick(0);  // dispatch first task
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_TRUE(state->task_planned_end_time.contains("P1"));
  EXPECT_EQ(10, state->task_planned_end_time.at("P1"));

  // P1 completes earlier than estimated.
  o.notify_task_completed("P1", 3);
  o.tick(3);  // planning
  const auto replanned = o.get_latest_schedule();
  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("P2", replanned.assignments[0].task_id);
  EXPECT_EQ(3, replanned.assignments[0].start_time);

  o.tick(4);  // dispatch
  state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_TRUE(state->completed_tasks.end() != std::ranges::find(state->completed_tasks, "P1"));
  EXPECT_EQ(3, state->task_actual_completion_time.at("P1"));
}

TEST(OrchestratorTest, LateCompletionTriggersReplanAndPushesScheduleOut) {
  to::Orchestrator o;
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 10, .deadline = {}});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});

  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  o.start();
  o.tick(0);  // dispatch first task
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_TRUE(state->task_planned_end_time.contains("P1"));
  EXPECT_EQ(5, state->task_planned_end_time.at("P1"));

  // P1 completes later than estimated.
  o.notify_task_completed("P1", 12);
  o.tick(12);  // planning
  const auto replanned = o.get_latest_schedule();
  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("P2", replanned.assignments[0].task_id);
  EXPECT_EQ(12, replanned.assignments[0].start_time);
}
}  // namespace
