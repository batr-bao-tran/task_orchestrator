#include "task_orchestrator/strategy/sjf_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(SJFStrategyTest, OrdersByDurationThenPriority) {
  to::Workflow w("wf");
  w.add_phase(
      to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P_long", "P_short", "P_mid"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{.id = "P_long",
                            .phase_id = "ph",
                            .sub_process_ids = {},
                            .estimated_duration = 100,
                            .priority = 0,
                            .deadline = {}});
  w.add_process(to::Process{.id = "P_short",
                            .phase_id = "ph",
                            .sub_process_ids = {},
                            .estimated_duration = 1,
                            .priority = 0,
                            .deadline = {}});
  w.add_process(to::Process{
      .id = "P_mid", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 5, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 3, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  to::SJFStrategy sjf;
  const auto result = to::Scheduler::plan(w, state, reg, 0, &sjf);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3U, result.assignments.size());
  // SJF: shortest first. P_short(1), P_mid(10), P_long(100). Ties: priority then id.
  EXPECT_EQ(result.assignments[0].task_id, "P_short");
  EXPECT_EQ(result.assignments[1].task_id, "P_mid");
  EXPECT_EQ(result.assignments[2].task_id, "P_long");
}

TEST(SJFStrategyTest, TieBreakByPriorityThenId) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"Pa", "Pb", "Pc"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "Pa", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  w.add_process(to::Process{
      .id = "Pb", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 10, .deadline = {}});
  w.add_process(to::Process{
      .id = "Pc", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 10, .deadline = {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 3, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  to::WorkflowState state;
  const to::SJFStrategy sjf;
  const auto result = to::Scheduler::plan(w, state, reg, 0, &sjf);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3U, result.assignments.size());
  // Same duration: higher priority first (Pb, Pc), then id (Pb < Pc), then Pa.
  EXPECT_EQ(result.assignments[0].task_id, "Pb");
  EXPECT_EQ(result.assignments[1].task_id, "Pc");
  EXPECT_EQ(result.assignments[2].task_id, "Pa");
}
}  // namespace
