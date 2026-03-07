#include "task_orchestrator/core/scheduler.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(SchedulerTest, EmptyWorkflow) {
  to::Workflow w("wf1");
  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 1, {{0, 1000}}, 0});
  to::Scheduler sched;
  auto result = sched.plan(w, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, SingleTask) {
  to::Workflow w("wf1");
  w.add_phase(to::Phase{"ph", "Ph", {"P1"}, {}});
  w.add_process(to::Process{"P1", "ph", {}, 10, 1, {}});
  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 1, {{0, 1000}}, 0});
  to::Scheduler sched;
  auto result = sched.plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1u, result.assignments.size());
  EXPECT_EQ("P1", result.assignments[0].task_id);
  EXPECT_EQ("A1", result.assignments[0].actor_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
}

TEST(SchedulerTest, PreferHigherPriority) {
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{"ph", "Ph", {"P1", "P2"}, {}});
  w2.add_process(to::Process{"P1", "ph", {}, 10, 0, {}});
  w2.add_process(to::Process{"P2", "ph", {}, 10, 10, {}});
  to::WorkflowState state;
  to::ActorRegistry reg2;
  reg2.add(to::Actor{"A1", 1, {{0, 1000}}, 0});
  to::Scheduler sched;
  auto result = sched.plan(w2, state, reg2, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1u, result.assignments.size());
  EXPECT_EQ("P2", result.assignments[0].task_id);
}

TEST(SchedulerTest, AlreadyAssignedTasksSkipped) {
  to::WorkflowState state;
  state.assigned_tasks = {"P1"};
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{"ph", "Ph", {"P1"}, {}});
  w3.add_process(to::Process{"P1", "ph", {}, 10, 0, {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 1, {{0, 1000}}, 0});
  to::Scheduler sched;
  auto result = sched.plan(w3, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, PhaseDependencies) {
  to::Workflow w4("wf4");
  w4.add_phase(to::Phase{"p1", "P1", {"P1a"}, {}});
  w4.add_phase(to::Phase{"p2", "P2", {"P2a"}, {"p1"}});
  w4.add_process(to::Process{"P1a", "p1", {}, 10, 0, {}});
  w4.add_process(to::Process{"P2a", "p2", {}, 10, 0, {}});
  to::WorkflowState state4;
  to::ActorRegistry reg4;
  reg4.add(to::Actor{"A1", 2, {{0, 1000}}, 0});
  to::Scheduler sched;
  auto result = sched.plan(w4, state4, reg4, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1u, result.assignments.size());
  EXPECT_EQ("P1a", result.assignments[0].task_id);
  state4.completed_phases = {"p1"};
  auto result2 = sched.plan(w4, state4, reg4, 0);
  ASSERT_EQ(1u, result2.assignments.size());
  EXPECT_EQ("P2a", result2.assignments[0].task_id);
}

TEST(SchedulerTest, PlanLazy) {
  to::Workflow w5("wf5");
  w5.add_phase(to::Phase{"ph", "Ph", {"P1", "P2"}, {}});
  w5.add_process(to::Process{"P1", "ph", {}, 5, 0, {}});
  w5.add_process(to::Process{"P2", "ph", {}, 5, 0, {}});
  to::WorkflowState state5;
  to::ActorRegistry reg5;
  reg5.add(to::Actor{"A1", 1, {{0, 1000}}, 0});
  reg5.add(to::Actor{"A2", 1, {{0, 1000}}, 0});
  to::Scheduler sched;
  size_t count = 0;
  for (const to::Assignment& a : sched.plan_lazy(w5, state5, reg5, 0)) {
    EXPECT_TRUE(a.actor_id == "A1" || a.actor_id == "A2");
    count++;
  }
  EXPECT_EQ(2u, count);
}
