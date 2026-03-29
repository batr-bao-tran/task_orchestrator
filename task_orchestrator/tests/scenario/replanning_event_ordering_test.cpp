#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "utils/sim_clock.hpp"

namespace {
namespace to = task_orchestrator;

void setup_orchestrator(to::Orchestrator& o) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 2, .priority = 10, .deadline = {}});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 2, .priority = 5, .deadline = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(
      to::Actor{.id = "A2", .capacity = 2, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.start();
  o.tick(0);
  o.tick(1);  // dispatch
}

TEST(ReplanningEventOrderingTest, FailThenUnavailableAtSameTimestamp) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, true); });
  clock.schedule_at(5, [&](to::SimClock::Time) { o.set_actor_unavailable("A1", true); });
  clock.schedule_at(5, [&](to::SimClock::Time t) { o.tick(t); });
  clock.schedule_at(6, [&](to::SimClock::Time t) { o.tick(t); });
  clock.run_until(10);

  const auto result = o.get_latest_schedule();
  ASSERT_TRUE(result.ok);
  const auto* final_state = o.workflow_state();
  ASSERT_NE(nullptr, final_state);
  auto owner = final_state->task_actor.find(failed_task);
  ASSERT_NE(owner, final_state->task_actor.end());
  EXPECT_EQ("A2", owner->second);
}

TEST(ReplanningEventOrderingTest, UnavailableThenFailAtSameTimestamp) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.set_actor_unavailable("A1", true); });
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, true); });
  clock.schedule_at(5, [&](to::SimClock::Time t) { o.tick(t); });
  clock.schedule_at(6, [&](to::SimClock::Time t) { o.tick(t); });
  clock.run_until(10);

  const auto result = o.get_latest_schedule();
  ASSERT_TRUE(result.ok);
  const auto* final_state = o.workflow_state();
  ASSERT_NE(nullptr, final_state);
  auto owner = final_state->task_actor.find(failed_task);
  ASSERT_NE(owner, final_state->task_actor.end());
  EXPECT_EQ("A2", owner->second);
}

TEST(ReplanningEventOrderingTest, ReplanTickBeforeTaskResumableDefersUntilNextTick) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, false); });
  clock.schedule_at(6, [&](to::SimClock::Time t) { o.tick(t); });  // no replan yet
  clock.schedule_at(7, [&](to::SimClock::Time) { o.mark_task_resumable(failed_task); });
  clock.schedule_at(8, [&](to::SimClock::Time t) { o.tick(t); });  // replan now
  clock.run_until(20);

  const auto result = o.get_latest_schedule();
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ(failed_task, result.assignments.front().task_id);
}

TEST(ReplanningEventOrderingTest, TaskResumableBeforeReplanTickSchedulesAtFirstTick) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, false); });
  clock.schedule_at(6, [&](to::SimClock::Time) { o.mark_task_resumable(failed_task); });
  clock.schedule_at(7, [&](to::SimClock::Time t) { o.tick(t); });  // first tick after resumable
  clock.run_until(20);

  const auto result = o.get_latest_schedule();
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ(failed_task, result.assignments.front().task_id);
}

TEST(ReplanningEventOrderingTest, TaskResumableThenTickAtSameTimestampSchedulesWithoutExtraDelay) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, false); });
  clock.schedule_at(6, [&](to::SimClock::Time) { o.mark_task_resumable(failed_task); });
  clock.schedule_at(6, [&](to::SimClock::Time t) { o.tick(t); });
  clock.schedule_at(7, [&](to::SimClock::Time t) { o.tick(t); });
  clock.run_until(7);

  const auto* final_state = o.workflow_state();
  ASSERT_NE(nullptr, final_state);
  auto owner = final_state->task_actor.find(failed_task);
  ASSERT_NE(owner, final_state->task_actor.end());
}

TEST(ReplanningEventOrderingTest, TickThenTaskResumableAtSameTimestampNeedsNextTick) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, false); });
  clock.schedule_at(6, [&](to::SimClock::Time t) { o.tick(t); });
  clock.schedule_at(6, [&](to::SimClock::Time) { o.mark_task_resumable(failed_task); });
  clock.run_until(7);

  const auto* deferred_state = o.workflow_state();
  ASSERT_NE(nullptr, deferred_state);
  EXPECT_EQ(deferred_state->task_actor.end(), deferred_state->task_actor.find(failed_task));

  clock.schedule_at(8, [&](to::SimClock::Time t) { o.tick(t); });
  clock.schedule_at(9, [&](to::SimClock::Time t) { o.tick(t); });
  clock.run_until(9);

  const auto* final_state = o.workflow_state();
  ASSERT_NE(nullptr, final_state);
  EXPECT_NE(final_state->task_actor.end(), final_state->task_actor.find(failed_task));
}

TEST(ReplanningEventOrderingTest, AvailabilityRestoredBeforeSameTimestampTickReusesRecoveredActor) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  to::SimClock clock;
  clock.schedule_at(5, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, true); });
  clock.schedule_at(5, [&](to::SimClock::Time) { o.set_actor_unavailable("A1", true); });
  clock.schedule_at(5, [&](to::SimClock::Time) { o.set_actor_unavailable("A1", false); });
  clock.schedule_at(5, [&](to::SimClock::Time t) { o.tick(t); });
  clock.schedule_at(6, [&](to::SimClock::Time t) { o.tick(t); });
  clock.run_until(10);

  const auto* final_state = o.workflow_state();
  ASSERT_NE(nullptr, final_state);
  auto owner = final_state->task_actor.find(failed_task);
  ASSERT_NE(owner, final_state->task_actor.end());
  EXPECT_EQ("A1", owner->second);
}

TEST(ReplanningEventOrderingTest, TriggerBurstProducesDeterministicSinglePlanOutcome) {
  to::Orchestrator o;
  setup_orchestrator(o);
  const auto* state = o.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_FALSE(state->assigned_tasks.empty());
  const auto failed_task = state->assigned_tasks.front();

  std::vector<std::string> snapshots;
  to::SimClock clock;
  clock.schedule_at(10, [&](to::SimClock::Time) { o.notify_task_failed(failed_task, true); });
  clock.schedule_at(10, [&](to::SimClock::Time) { o.set_actor_unavailable("A1", true); });
  clock.schedule_at(10, [&](to::SimClock::Time) { o.set_actor_unavailable("A1", true); });
  clock.schedule_at(10, [&](to::SimClock::Time t) {
    o.tick(t);
    o.tick(t + 1);
    const auto* final_state = o.workflow_state();
    if (final_state) {
      auto owner = final_state->task_actor.find(failed_task);
      if (owner != final_state->task_actor.end()) {
        snapshots.push_back(owner->second + ":" + failed_task);
      }
    }
  });
  clock.run_until(15);

  ASSERT_EQ(1U, snapshots.size());
  EXPECT_EQ("A2:" + failed_task, snapshots.front());
}
}  // namespace
