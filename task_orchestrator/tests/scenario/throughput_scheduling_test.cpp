#include <gtest/gtest.h>

#include <set>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(ThroughputSchedulingTest, TwoActorsTwoTasks) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2", "P3"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  w.add_process(to::Process{
      .id = "P3", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  reg.add(to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  EXPECT_LE(result.assignments.size(), 2U);
  std::set<to::TaskId> assigned;
  for (const auto& a : result.assignments) {
    EXPECT_EQ(0U, assigned.count(a.task_id));
    assigned.insert(a.task_id);
  }
}

TEST(ThroughputSchedulingTest, PriorityOrder) {
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P_low", "P_high"}, .dependency_phase_ids = {}});
  w2.add_process(to::Process{
      .id = "P_low", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  w2.add_process(to::Process{.id = "P_high",
                             .phase_id = "ph",
                             .sub_process_ids = {},
                             .estimated_duration = 1,
                             .priority = 10,
                             .deadline = {}});
  to::ActorRegistry reg2;
  reg2.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  to::WorkflowState state;
  const auto result = to::Scheduler::plan(w2, state, reg2, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P_high", result.assignments[0].task_id);
}
}  // namespace
