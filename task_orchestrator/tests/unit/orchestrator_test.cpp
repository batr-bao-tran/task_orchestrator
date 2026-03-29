#include "task_orchestrator/core/orchestrator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "task_orchestrator/strategy/scheduling_strategy.hpp"

namespace {
namespace to = task_orchestrator;

class PreserveOrderStrategy final : public to::SchedulingStrategy {
 public:
  void order_tasks(std::vector<to::TaskInfo>&) const override {}
};

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

TEST(OrchestratorTest, CompletingAllPhasesReturnsPlannerToIdle) {
  to::Orchestrator orchestrator;
  to::Workflow workflow("wf_complete_all");
  workflow.add_phase(to::Phase{.id = "p1", .name = "P1", .process_ids = {}, .dependency_phase_ids = {}});
  workflow.add_phase(to::Phase{.id = "p2", .name = "P2", .process_ids = {}, .dependency_phase_ids = {"p1"}});
  orchestrator.set_workflow(std::move(workflow));
  orchestrator.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});

  orchestrator.start();
  orchestrator.complete_phase("p1");
  EXPECT_NE(to::PlannerState::Idle, orchestrator.current_planner_state());

  orchestrator.complete_phase("p2");
  EXPECT_EQ(to::PlannerState::Idle, orchestrator.current_planner_state());
}

TEST(OrchestratorTest, StrategyConfigurationAndRankingProfileAreAppliedWithoutCrashing) {
  to::Orchestrator orchestrator;
  to::Workflow workflow("wf_strategy");
  workflow.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 3, .priority = 5, .deadline = {}});
  orchestrator.set_workflow(std::move(workflow));
  orchestrator.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  orchestrator.set_scheduling_strategy(nullptr);
  orchestrator.set_actor_ranking_profile(
      {.criteria = {to::ActorRankingCriterion::LeastLoaded, to::ActorRankingCriterion::PreferredActor}});
  orchestrator.set_scheduling_strategy(std::make_unique<PreserveOrderStrategy>());

  orchestrator.start();
  orchestrator.tick(0);
  const auto schedule = orchestrator.get_latest_schedule();
  ASSERT_TRUE(schedule.ok);
  ASSERT_EQ(1U, schedule.assignments.size());
  EXPECT_EQ("P1", schedule.assignments.front().task_id);
}

TEST(OrchestratorTest, TickIsNoOpBeforeStartAndRestartsPlanningFromIdleAfterCompletion) {
  to::Orchestrator orchestrator;
  to::Workflow workflow("wf_idle");
  workflow.add_phase(to::Phase{.id = "only_phase", .name = "Only", .process_ids = {}, .dependency_phase_ids = {}});
  orchestrator.set_workflow(std::move(workflow));
  orchestrator.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  orchestrator.tick(0);
  EXPECT_EQ(to::PlannerState::Idle, orchestrator.current_planner_state());

  orchestrator.start();
  orchestrator.complete_phase("only_phase");
  EXPECT_EQ(to::PlannerState::Idle, orchestrator.current_planner_state());

  orchestrator.tick(5);
  EXPECT_EQ(to::PlannerState::Planning, orchestrator.current_planner_state());
}

TEST(OrchestratorTest, RestoringActorAvailabilityRequestsReplan) {
  to::Orchestrator orchestrator;
  to::Workflow workflow("wf_restore");
  workflow.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 2, .priority = 1, .deadline = {}});
  orchestrator.set_workflow(std::move(workflow));
  orchestrator.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  orchestrator.start();
  orchestrator.tick(0);
  orchestrator.tick(1);
  const auto* state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  orchestrator.notify_task_failed(state->assigned_tasks.front(), true);
  orchestrator.set_actor_unavailable("A1", true);
  orchestrator.tick(2);
  EXPECT_TRUE(orchestrator.get_latest_schedule().assignments.empty());

  orchestrator.set_actor_unavailable("A1", false);
  state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_TRUE(state->assigned_tasks.empty());
  EXPECT_EQ(std::vector<to::TaskId>({"P1"}), state->resumable_tasks);
  EXPECT_TRUE(state->unavailable_actors.empty());
  const auto direct_replanned =
      to::Scheduler::plan(*orchestrator.workflow(), *state, *orchestrator.actor_registry(), 3);
  ASSERT_TRUE(direct_replanned.ok);
  ASSERT_EQ(1U, direct_replanned.assignments.size());
  orchestrator.tick(3);
  orchestrator.tick(4);
  const auto replanned = orchestrator.get_latest_schedule();
  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("A1", replanned.assignments.front().actor_id);
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

TEST(OrchestratorTest, FutureAssignmentsAreNotDispatchedBeforeTheirStartTime) {
  to::Orchestrator orchestrator;
  to::Workflow workflow("wf_future_dispatch");
  workflow.add_phase(to::Phase{.id = "ph", .name = "Phase", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 10, .deadline = {}});
  workflow.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 4, .priority = 1, .deadline = {}});

  orchestrator.set_workflow(std::move(workflow));
  orchestrator.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  orchestrator.start();
  orchestrator.tick(0);  // dispatch work that starts at time 0
  const auto initial_schedule = orchestrator.get_latest_schedule();
  ASSERT_TRUE(initial_schedule.ok);
  ASSERT_EQ(2U, initial_schedule.assignments.size());
  EXPECT_EQ(0, initial_schedule.assignments[0].start_time);
  EXPECT_EQ(5, initial_schedule.assignments[1].start_time);

  const auto* state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_EQ(std::vector<to::TaskId>({"P1"}), state->assigned_tasks);
  EXPECT_FALSE(state->task_actor.contains("P2"));

  orchestrator.tick(4);
  state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_EQ(std::vector<to::TaskId>({"P1"}), state->assigned_tasks);
  EXPECT_FALSE(state->task_actor.contains("P2"));

  orchestrator.notify_task_completed("P1", 5);
  state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  const auto direct_after_completion =
      to::Scheduler::plan(*orchestrator.workflow(), *state, *orchestrator.actor_registry(), 5);
  ASSERT_TRUE(direct_after_completion.ok);
  ASSERT_EQ(1U, direct_after_completion.assignments.size());
  orchestrator.tick(5);
  orchestrator.tick(6);
  state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_TRUE(std::ranges::find(state->assigned_tasks, "P2") != state->assigned_tasks.end());
  EXPECT_EQ("A1", state->task_actor.at("P2"));
}
}  // namespace
