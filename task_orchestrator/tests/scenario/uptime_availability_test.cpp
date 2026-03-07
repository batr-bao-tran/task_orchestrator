#include <gtest/gtest.h>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

TEST(UptimeAvailabilityTest, ScheduleRespectsWindows) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  to::Actor a;
  a.id = "A1";
  a.capacity = 1;
  a.current_load = 0;
  a.availability_windows = {{.start = 10, .end = 20}, {.start = 30, .end = 40}};
  reg.add(std::move(a));

  to::WorkflowState state;
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_GE(result.assignments[0].start_time, 10);
  EXPECT_LE(result.assignments[0].start_time + 5, 20);
}

TEST(UptimeAvailabilityTest, CanAcceptAt) {
  to::Actor a2;
  a2.id = "A1";
  a2.capacity = 1;
  a2.current_load = 0;
  a2.availability_windows = {{.start = 100, .end = 200}};
  EXPECT_FALSE(a2.can_accept_at(50, 10));
  EXPECT_TRUE(a2.can_accept_at(100, 10));
  EXPECT_FALSE(a2.can_accept_at(195, 10));
}
}  // namespace
