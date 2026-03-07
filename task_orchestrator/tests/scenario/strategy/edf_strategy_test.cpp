#include "task_orchestrator/strategy/edf_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(EDFStrategyTest, OrdersByPriorityThenDeadline) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2", "P3"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "P1",
                                   .phase_id = "ph",
                                   .sub_process_ids = {},
                                   .estimated_duration = 5,
                                   .priority = 1,
                                   .deadline = 100});  // low prio, deadline 100
  workflow.add_process(to::Process{.id = "P2",
                                   .phase_id = "ph",
                                   .sub_process_ids = {},
                                   .estimated_duration = 5,
                                   .priority = 10,
                                   .deadline = 50});  // high prio, deadline 50
  workflow.add_process(to::Process{.id = "P3",
                                   .phase_id = "ph",
                                   .sub_process_ids = {},
                                   .estimated_duration = 5,
                                   .priority = 10,
                                   .deadline = 30});  // high prio, earlier deadline

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 3, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  to::EDFStrategy edf;
  const auto result = to::Scheduler::plan(workflow, state, reg, 0, &edf);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3U, result.assignments.size());
  // EDF: higher priority first, then earlier deadline. So P3 (10, 30), P2 (10, 50), P1 (1, 100).
  EXPECT_EQ(result.assignments[0].task_id, "P3");
  EXPECT_EQ(result.assignments[1].task_id, "P2");
  EXPECT_EQ(result.assignments[2].task_id, "P1");
}

TEST(EDFStrategyTest, DefaultPlanUsesEDF) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"Pa", "Pb"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{
      .id = "Pa", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = 200});
  workflow.add_process(to::Process{
      .id = "Pb", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 5, .deadline = 100});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  const auto result = to::Scheduler::plan(workflow, state, reg, 0);  // null strategy => EDF
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ(result.assignments[0].task_id, "Pb");  // higher priority
  EXPECT_EQ(result.assignments[1].task_id, "Pa");
}
}  // namespace
