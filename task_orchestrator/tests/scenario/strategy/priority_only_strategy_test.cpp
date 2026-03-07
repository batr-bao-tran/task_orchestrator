#include "task_orchestrator/strategy/priority_only_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(PriorityOnlyStrategyTest, OrdersByPriorityOnly) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2", "P3"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 1, .deadline = 100});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 10, .deadline = 200});
  w.add_process(to::Process{
      .id = "P3", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 5, .deadline = 50});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 3, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  to::PriorityOnlyStrategy prio;
  auto result = to::Scheduler::plan(w, state, reg, 0, &prio);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3U, result.assignments.size());
  // Priority only: 10, 5, 1. Ties by id. So P2(10), P3(5), P1(1).
  EXPECT_EQ(result.assignments[0].task_id, "P2");
  EXPECT_EQ(result.assignments[1].task_id, "P3");
  EXPECT_EQ(result.assignments[2].task_id, "P1");
}

TEST(PriorityOnlyStrategyTest, IgnoresDeadline) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"Pa", "Pb"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{.id = "Pa",
                            .phase_id = "ph",
                            .sub_process_ids = {},
                            .estimated_duration = 1,
                            .priority = 5,
                            .deadline = 10});  // earlier deadline
  w.add_process(to::Process{
      .id = "Pb", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 5, .deadline = 100});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  to::PriorityOnlyStrategy prio;
  auto result = to::Scheduler::plan(w, state, reg, 0, &prio);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  // Same priority; tie-break by id: Pa < Pb
  EXPECT_EQ(result.assignments[0].task_id, "Pa");
  EXPECT_EQ(result.assignments[1].task_id, "Pb");
}
}  // namespace
