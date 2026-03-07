#include "task_orchestrator/strategy/priority_only_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(PriorityOnlyStrategyTest, OrdersByPriorityOnly) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"P1", "P2", "P3"}, {}});
  w.add_process(to::Process{"P1", "ph", {}, 1, 1, 100});
  w.add_process(to::Process{"P2", "ph", {}, 1, 10, 200});
  w.add_process(to::Process{"P3", "ph", {}, 1, 5, 50});

  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 3, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::PriorityOnlyStrategy prio;
  auto result = sched.plan(w, state, reg, 0, &prio);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3u, result.assignments.size());
  // Priority only: 10, 5, 1. Ties by id. So P2(10), P3(5), P1(1).
  EXPECT_EQ(result.assignments[0].task_id, "P2");
  EXPECT_EQ(result.assignments[1].task_id, "P3");
  EXPECT_EQ(result.assignments[2].task_id, "P1");
}

TEST(PriorityOnlyStrategyTest, IgnoresDeadline) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"Pa", "Pb"}, {}});
  w.add_process(to::Process{"Pa", "ph", {}, 1, 5, 10});  // earlier deadline
  w.add_process(to::Process{"Pb", "ph", {}, 1, 5, 100});
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 2, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::PriorityOnlyStrategy prio;
  auto result = sched.plan(w, state, reg, 0, &prio);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2u, result.assignments.size());
  // Same priority; tie-break by id: Pa < Pb
  EXPECT_EQ(result.assignments[0].task_id, "Pa");
  EXPECT_EQ(result.assignments[1].task_id, "Pb");
}
