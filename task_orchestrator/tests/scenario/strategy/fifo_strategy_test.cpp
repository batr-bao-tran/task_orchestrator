#include "task_orchestrator/strategy/fifo_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(FIFOStrategyTest, OrdersByPhaseThenTaskId) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"Pz", "Pa", "Pm"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "Pz", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  w.add_process(to::Process{
      .id = "Pa", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  w.add_process(to::Process{
      .id = "Pm", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 3, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  const to::FIFOStrategy fifo;
  const auto result = to::Scheduler::plan(w, state, reg, 0, &fifo);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3U, result.assignments.size());
  // FIFO: phase order then task id. Phase order is insertion order; task ids Pa, Pm, Pz.
  EXPECT_EQ(result.assignments[0].task_id, "Pa");
  EXPECT_EQ(result.assignments[1].task_id, "Pm");
  EXPECT_EQ(result.assignments[2].task_id, "Pz");
}

TEST(FIFOStrategyTest, IgnoresPriority) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P_high", "P_low"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{.id = "P_high",
                            .phase_id = "ph",
                            .sub_process_ids = {},
                            .estimated_duration = 1,
                            .priority = 100,
                            .deadline = {}});
  w.add_process(to::Process{
      .id = "P_low", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  const to::FIFOStrategy fifo;
  const auto result = to::Scheduler::plan(w, state, reg, 0, &fifo);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  // FIFO orders by phase then id: P_high < P_low lexicographically
  EXPECT_EQ(result.assignments[0].task_id, "P_high");
  EXPECT_EQ(result.assignments[1].task_id, "P_low");
}
}  // namespace
