#include "task_orchestrator/core/orchestrator.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(OrchestratorTest, SetWorkflowAndRegisterActor) {
  to::Orchestrator o;
  to::Workflow w("wf1");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {}, .dependency_phase_ids = {}});
  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  ASSERT_NE(nullptr, o.workflow());
  EXPECT_EQ("wf1", o.workflow()->id());
  ASSERT_NE(nullptr, o.actor_registry());
  EXPECT_EQ(1U, o.actor_registry()->actor_ids().size());
}

TEST(OrchestratorTest, StartAndTick) {
  to::Orchestrator o2;
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w2.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  o2.set_workflow(std::move(w2));
  o2.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
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
  w3.add_phase(to::Phase{.id = "p1", .name = "P1", .process_ids = {}, .dependency_phase_ids = {}});
  w3.add_phase(to::Phase{.id = "p2", .name = "P2", .process_ids = {}, .dependency_phase_ids = {"p1"}});
  o3.set_workflow(std::move(w3));
  o3.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  o3.start();
  o3.complete_phase("p1");
  const auto* state = o3.workflow_state();
  ASSERT_NE(nullptr, state);
  ASSERT_EQ(1U, state->completed_phases.size());
  EXPECT_EQ("p1", state->completed_phases[0]);
}
}  // namespace
