#include "task_orchestrator/core/orchestrator.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(OrchestratorTest, SetWorkflowAndRegisterActor) {
  to::Orchestrator o;
  to::Workflow w("wf1");
  w.add_phase(to::Phase{"ph", "Ph", {}, {}});
  o.set_workflow(std::move(w));
  o.register_actor(to::Actor{"A1", 1, {{0, 1000}}, 0});
  ASSERT_NE(nullptr, o.workflow());
  EXPECT_EQ("wf1", o.workflow()->id());
  ASSERT_NE(nullptr, o.actor_registry());
  EXPECT_EQ(1u, o.actor_registry()->actor_ids().size());
}

TEST(OrchestratorTest, StartAndTick) {
  to::Orchestrator o2;
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{"ph", "Ph", {"P1"}, {}});
  w2.add_process(to::Process{"P1", "ph", {}, 10, 0, {}});
  o2.set_workflow(std::move(w2));
  o2.register_actor(to::Actor{"A1", 1, {{0, 1000}}, 0});
  o2.start();
  EXPECT_TRUE(o2.current_planner_state() == to::PlannerState::Planning ||
              o2.current_planner_state() == to::PlannerState::Dispatching);

  o2.tick(0);
  auto sched = o2.get_latest_schedule();
  EXPECT_TRUE(sched.ok);
}

TEST(OrchestratorTest, CompletePhase) {
  to::Orchestrator o3;
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{"p1", "P1", {}, {}});
  w3.add_phase(to::Phase{"p2", "P2", {}, {"p1"}});
  o3.set_workflow(std::move(w3));
  o3.register_actor(to::Actor{"A1", 1, {{0, 1000}}, 0});
  o3.start();
  o3.complete_phase("p1");
  auto* state = o3.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_EQ(1u, state->completed_phases.size());
  EXPECT_EQ("p1", state->completed_phases[0]);
}
