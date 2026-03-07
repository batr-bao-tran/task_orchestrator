#include "task_orchestrator/core/workflow.hpp"

#include <gtest/gtest.h>

#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(WorkflowTest, AddPhaseAndProcess) {
  to::Workflow w("wf1");
  to::Phase p;
  p.id = "phase1";
  p.name = "Phase 1";
  p.process_ids = {};
  p.dependency_phase_ids = {};
  w.add_phase(std::move(p));
  auto* ph = w.phase("phase1");
  ASSERT_NE(nullptr, ph);
  EXPECT_EQ("phase1", ph->id);
  EXPECT_EQ("Phase 1", ph->name);

  to::Process proc;
  proc.id = "P1";
  proc.phase_id = "phase1";
  w.add_process(std::move(proc));
  auto* pr = w.process("P1");
  ASSERT_NE(nullptr, pr);
  EXPECT_EQ("P1", pr->id);
}

TEST(WorkflowTest, RootPhasesAndReadyPhases) {
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{"p1", "P1", {}, {}});
  w2.add_phase(to::Phase{"p2", "P2", {}, {"p1"}});
  auto roots = w2.root_phases();
  ASSERT_EQ(1u, roots.size());
  EXPECT_EQ("p1", roots[0]);

  w2.add_phase(to::Phase{"p3", "P3", {}, {"p1"}});
  w2.add_phase(to::Phase{"p4", "P4", {}, {"p2", "p3"}});
  auto ready_none = w2.ready_phases({});
  EXPECT_EQ(1u, ready_none.size());
  EXPECT_EQ("p1", ready_none[0]);
  auto ready_after_p1 = w2.ready_phases({"p1"});
  ASSERT_EQ(2u, ready_after_p1.size());
  auto ready_after_p2_p3 = w2.ready_phases({"p1", "p2", "p3"});
  ASSERT_EQ(1u, ready_after_p2_p3.size());
  EXPECT_EQ("p4", ready_after_p2_p3[0]);
}

TEST(WorkflowTest, TaskIdsForPhaseAndProcessForTask) {
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{"ph", "Ph", {"P1", "P2"}, {}});
  w3.add_process(to::Process{"P1", "ph", {}, 5, 0, {}});
  w3.add_process(to::Process{"P2", "ph", {"SP1"}, 5, 0, {}});
  auto tids = w3.task_ids_for_phase("ph");
  ASSERT_EQ(3u, tids.size());
  EXPECT_EQ("P1", tids[0]);
  EXPECT_EQ("P2", tids[1]);
  EXPECT_EQ("SP1", tids[2]);

  ASSERT_NE(nullptr, w3.process_for_task("P1"));
  ASSERT_NE(nullptr, w3.process_for_task("SP1"));
  EXPECT_EQ(nullptr, w3.process_for_task("unknown"));
}
