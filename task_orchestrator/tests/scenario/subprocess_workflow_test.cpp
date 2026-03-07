#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(SubprocessWorkflowTest, TaskIdsAndSchedule) {
  to::Workflow w("sub_wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Phase", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  to::Process p1;
  p1.id = "P1";
  p1.phase_id = "ph";
  p1.sub_process_ids = {"SP1", "SP2"};
  p1.estimated_duration = 5;
  p1.priority = 0;
  w.add_process(std::move(p1));

  const auto tids = w.task_ids_for_phase("ph");
  ASSERT_EQ(3U, tids.size());
  EXPECT_EQ("P1", tids[0]);
  EXPECT_EQ("SP1", tids[1]);
  EXPECT_EQ("SP2", tids[2]);

  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 3, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(3U, result.assignments.size());
}
}  // namespace
