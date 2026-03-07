#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace to = task_orchestrator;

TEST(SubprocessWorkflowTest, TaskIdsAndSchedule) {
  to::Workflow w("sub_wf");
  w.add_phase(to::Phase{"ph", "Phase", {"P1"}, {}});
  to::Process p1;
  p1.id = "P1";
  p1.phase_id = "ph";
  p1.sub_process_ids = {"SP1", "SP2"};
  p1.estimated_duration = 5;
  p1.priority = 0;
  w.add_process(std::move(p1));

  auto tids = w.task_ids_for_phase("ph");
  ASSERT_EQ(3u, tids.size());
  EXPECT_EQ("P1", tids[0]);
  EXPECT_EQ("SP1", tids[1]);
  EXPECT_EQ("SP2", tids[2]);

  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{"A1", 3, {{0, 1000}}, 0});
  to::Scheduler sched;
  auto result = sched.plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(3u, result.assignments.size());
}
