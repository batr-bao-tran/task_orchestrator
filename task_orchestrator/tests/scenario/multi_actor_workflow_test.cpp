#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(MultiActorWorkflowTest, TwoPhasesTwoWorkers) {
  to::Workflow w("scenario_wf");
  w.add_phase(to::Phase{.id = "phase1", .name = "Phase 1", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w.add_phase(to::Phase{.id = "phase2", .name = "Phase 2", .process_ids = {"P3"}, .dependency_phase_ids = {"phase1"}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "phase1", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "phase1", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});
  w.add_process(to::Process{.id = "P3",
                            .phase_id = "phase2",
                            .sub_process_ids = {},
                            .estimated_duration = 10,
                            .priority = 0,
                            .deadline = {}});

  to::Orchestrator o;
  o.set_workflow(std::move(w));
  o.register_actor(to::Actor{
      .id = "worker_a", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(to::Actor{
      .id = "worker_b", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  o.start();
  o.tick(0);
  auto sched = o.get_latest_schedule();
  ASSERT_TRUE(sched.ok);
  EXPECT_GE(sched.assignments.size(), 1U);
  EXPECT_LE(sched.assignments.size(), 2U);
  for (const auto& a : sched.assignments) {
    EXPECT_TRUE(a.actor_id == "worker_a" || a.actor_id == "worker_b");
    EXPECT_TRUE(a.task_id == "P1" || a.task_id == "P2");
  }
  o.complete_phase("phase1");
  o.tick(10);
  sched = o.get_latest_schedule();
  EXPECT_TRUE(sched.ok);
}
}  // namespace
