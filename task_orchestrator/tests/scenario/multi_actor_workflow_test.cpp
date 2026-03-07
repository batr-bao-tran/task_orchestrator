#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(MultiActorWorkflowTest, TwoPhasesTwoWorkers) {
  to::Workflow w("scenario_wf");
  w.add_phase(to::Phase{"phase1", "Phase 1", {"P1", "P2"}, {}});
  w.add_phase(to::Phase{"phase2", "Phase 2", {"P3"}, {"phase1"}});
  w.add_process(to::Process{"P1", "phase1", {}, 5, 1, {}});
  w.add_process(to::Process{"P2", "phase1", {}, 5, 1, {}});
  w.add_process(to::Process{"P3", "phase2", {}, 10, 0, {}});

  to::Orchestrator o;
  o.set_workflow(std::move(w));
  o.register_actor(to::Actor{"worker_a", 1, {{0, 100}}, 0});
  o.register_actor(to::Actor{"worker_b", 1, {{0, 100}}, 0});

  o.start();
  o.tick(0);
  auto sched = o.get_latest_schedule();
  ASSERT_TRUE(sched.ok);
  EXPECT_GE(sched.assignments.size(), 1u);
  EXPECT_LE(sched.assignments.size(), 2u);
  for (const auto& a : sched.assignments) {
    EXPECT_TRUE(a.actor_id == "worker_a" || a.actor_id == "worker_b");
    EXPECT_TRUE(a.task_id == "P1" || a.task_id == "P2");
  }
  o.complete_phase("phase1");
  o.tick(10);
  sched = o.get_latest_schedule();
  EXPECT_TRUE(sched.ok);
}
