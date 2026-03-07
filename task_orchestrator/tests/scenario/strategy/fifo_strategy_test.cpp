#include "task_orchestrator/strategy/fifo_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(FIFOStrategyTest, OrdersByPhaseThenTaskId) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"Pz", "Pa", "Pm"}, {}});
  w.add_process(to::Process{"Pz", "ph", {}, 1, 0, {}});
  w.add_process(to::Process{"Pa", "ph", {}, 1, 0, {}});
  w.add_process(to::Process{"Pm", "ph", {}, 1, 0, {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 3, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::FIFOStrategy fifo;
  auto result = sched.plan(w, state, reg, 0, &fifo);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3u, result.assignments.size());
  // FIFO: phase order then task id. Phase order is insertion order; task ids Pa, Pm, Pz.
  EXPECT_EQ(result.assignments[0].task_id, "Pa");
  EXPECT_EQ(result.assignments[1].task_id, "Pm");
  EXPECT_EQ(result.assignments[2].task_id, "Pz");
}

TEST(FIFOStrategyTest, IgnoresPriority) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"P_high", "P_low"}, {}});
  w.add_process(to::Process{"P_high", "ph", {}, 1, 100, {}});
  w.add_process(to::Process{"P_low", "ph", {}, 1, 0, {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 2, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::FIFOStrategy fifo;
  auto result = sched.plan(w, state, reg, 0, &fifo);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2u, result.assignments.size());
  // FIFO orders by phase then id: P_high < P_low lexicographically
  EXPECT_EQ(result.assignments[0].task_id, "P_high");
  EXPECT_EQ(result.assignments[1].task_id, "P_low");
}
