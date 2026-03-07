#include <gtest/gtest.h>

#include <set>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(ThroughputSchedulingTest, TwoActorsTwoTasks) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"P1", "P2", "P3"}, {}});
  w.add_process(to::Process{"P1", "ph", {}, 1, 0, {}});
  w.add_process(to::Process{"P2", "ph", {}, 1, 0, {}});
  w.add_process(to::Process{"P3", "ph", {}, 1, 0, {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 1, {{0, 100}}, 0});
  reg.add(to::Actor{"A2", 1, {{0, 100}}, 0});

  to::WorkflowState state;
  to::Scheduler sched;
  auto result = sched.plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  EXPECT_LE(result.assignments.size(), 2u);
  std::set<to::TaskId> assigned;
  for (const auto& a : result.assignments) {
    EXPECT_EQ(0u, assigned.count(a.task_id));
    assigned.insert(a.task_id);
  }
}

TEST(ThroughputSchedulingTest, PriorityOrder) {
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{"ph", "Ph", {"P_low", "P_high"}, {}});
  w2.add_process(to::Process{"P_low", "ph", {}, 1, 0, {}});
  w2.add_process(to::Process{"P_high", "ph", {}, 1, 10, {}});
  to::ActorRegistry reg2;
  reg2.add(to::Actor{"A1", 1, {{0, 100}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  auto result = sched.plan(w2, state, reg2, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1u, result.assignments.size());
  EXPECT_EQ("P_high", result.assignments[0].task_id);
}
