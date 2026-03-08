#include "task_orchestrator/core/scheduler.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(SchedulerTest, EmptyWorkflow) {
  to::Workflow w("wf1");
  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, SingleTask) {
  to::Workflow w("wf1");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 1, .deadline = {}});
  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P1", result.assignments[0].task_id);
  EXPECT_EQ("A1", result.assignments[0].actor_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
}

TEST(SchedulerTest, PreferHigherPriority) {
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w2.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  w2.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 10, .deadline = {}});
  to::WorkflowState state;
  to::ActorRegistry reg2;
  reg2.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w2, state, reg2, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P2", result.assignments[0].task_id);
}

TEST(SchedulerTest, AlreadyAssignedTasksSkipped) {
  to::WorkflowState state;
  state.assigned_tasks = {"P1"};
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w3.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w3, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, PhaseDependencies) {
  to::Workflow w4("wf4");
  w4.add_phase(to::Phase{.id = "p1", .name = "P1", .process_ids = {"P1a"}, .dependency_phase_ids = {}});
  w4.add_phase(to::Phase{.id = "p2", .name = "P2", .process_ids = {"P2a"}, .dependency_phase_ids = {"p1"}});
  w4.add_process(to::Process{
      .id = "P1a", .phase_id = "p1", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  w4.add_process(to::Process{
      .id = "P2a", .phase_id = "p2", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  to::WorkflowState state4;
  to::ActorRegistry reg4;
  reg4.add(
      to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w4, state4, reg4, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P1a", result.assignments[0].task_id);
  state4.completed_phases = {"p1"};
  const auto result2 = to::Scheduler::plan(w4, state4, reg4, 0);
  ASSERT_EQ(1U, result2.assignments.size());
  EXPECT_EQ("P2a", result2.assignments[0].task_id);
}

TEST(SchedulerTest, PlanLazy) {
  to::Workflow w5("wf5");
  w5.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w5.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});
  w5.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});
  to::WorkflowState state5;
  to::ActorRegistry reg5;
  reg5.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  reg5.add(
      to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  size_t count = 0;
  for (const to::Assignment& a : to::Scheduler::plan_lazy(w5, state5, reg5, 0)) {
    EXPECT_TRUE(a.actor_id == "A1" || a.actor_id == "A2") << "actor_id=" << a.actor_id;
    count++;
  }
  EXPECT_EQ(2U, count);
}
}  // namespace
