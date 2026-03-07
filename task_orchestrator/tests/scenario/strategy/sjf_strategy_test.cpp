#include "task_orchestrator/strategy/sjf_strategy.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(SJFStrategyTest, OrdersByDurationThenPriority) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"P_long", "P_short", "P_mid"}, {}});
  w.add_process(to::Process{"P_long", "ph", {}, 100, 0, {}});
  w.add_process(to::Process{"P_short", "ph", {}, 1, 0, {}});
  w.add_process(to::Process{"P_mid", "ph", {}, 10, 5, {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 3, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::SJFStrategy sjf;
  auto result = sched.plan(w, state, reg, 0, &sjf);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3u, result.assignments.size());
  // SJF: shortest first. P_short(1), P_mid(10), P_long(100). Ties: priority then id.
  EXPECT_EQ(result.assignments[0].task_id, "P_short");
  EXPECT_EQ(result.assignments[1].task_id, "P_mid");
  EXPECT_EQ(result.assignments[2].task_id, "P_long");
}

TEST(SJFStrategyTest, TieBreakByPriorityThenId) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{"ph", "Ph", {"Pa", "Pb", "Pc"}, {}});
  w.add_process(to::Process{"Pa", "ph", {}, 5, 1, {}});
  w.add_process(to::Process{"Pb", "ph", {}, 5, 10, {}});
  w.add_process(to::Process{"Pc", "ph", {}, 5, 10, {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 3, {{0, 1000}}, 0});
  to::WorkflowState state;
  to::Scheduler sched;
  to::SJFStrategy sjf;
  auto result = sched.plan(w, state, reg, 0, &sjf);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(3u, result.assignments.size());
  // Same duration: higher priority first (Pb, Pc), then id (Pb < Pc), then Pa.
  EXPECT_EQ(result.assignments[0].task_id, "Pb");
  EXPECT_EQ(result.assignments[1].task_id, "Pc");
  EXPECT_EQ(result.assignments[2].task_id, "Pa");
}
