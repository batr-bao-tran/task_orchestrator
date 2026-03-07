#include "task_orchestrator/strategy/edf_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(EDFStrategyTest, OrdersByPriorityThenDeadline) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"P1", "P2", "P3"}, {}});
  w.add_process(to::Process{"P1", "ph", {}, 5, 1, 100});  // low prio, deadline 100
  w.add_process(to::Process{"P2", "ph", {}, 5, 10, 50});  // high prio, deadline 50
  w.add_process(to::Process{"P3", "ph", {}, 5, 10, 30});  // high prio, earlier deadline

  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 3, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::EDFStrategy edf;
  auto result = sched.plan(w, state, reg, 0, &edf);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3u, result.assignments.size());
  // EDF: higher priority first, then earlier deadline. So P3 (10, 30), P2 (10, 50), P1 (1, 100).
  EXPECT_EQ(result.assignments[0].task_id, "P3");
  EXPECT_EQ(result.assignments[1].task_id, "P2");
  EXPECT_EQ(result.assignments[2].task_id, "P1");
}

TEST(EDFStrategyTest, DefaultPlanUsesEDF) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"Pa", "Pb"}, {}});
  w.add_process(to::Process{"Pa", "ph", {}, 1, 0, 200});
  w.add_process(to::Process{"Pb", "ph", {}, 1, 5, 100});
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 2, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  auto result = sched.plan(w, state, reg, 0);  // null strategy => EDF
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2u, result.assignments.size());
  EXPECT_EQ(result.assignments[0].task_id, "Pb");  // higher priority
  EXPECT_EQ(result.assignments[1].task_id, "Pa");
}
