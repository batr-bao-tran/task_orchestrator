#include "control_plane/service/plan_diff.hpp"

#include <gtest/gtest.h>

namespace {
namespace tcs = task_orchestrator::control_plane::service;
namespace tp = task_orchestrator::protocol;

tp::RuntimeApiResponse make_response(
    std::initializer_list<std::tuple<const char*, const char*, int, int>> assignments) {
  tp::RuntimeApiResponse response;
  response.set_ok(true);
  response.mutable_result()->set_ok(true);
  for (const auto& [task_id, actor_id, start_time, end_time] : assignments) {
    auto* assignment = response.mutable_result()->add_assignments();
    assignment->set_task_id(task_id);
    assignment->set_actor_id(actor_id);
    assignment->set_start_time(start_time);
    assignment->set_end_time(end_time);
  }
  return response;
}

TEST(PlanDiffTest, DetectsAddedRemovedAndChangedAssignments) {
  const tp::RuntimeApiResponse before = make_response({
      {"pick", "robot_1", 0, 5},
      {"pack", "robot_2", 5, 10},
  });
  const tp::RuntimeApiResponse after = make_response({
      {"pick", "robot_3", 1, 6},
      {"ship", "robot_2", 10, 12},
  });

  const tp::WorkflowPlanDiff diff = tcs::diff_plan_versions(before, after);
  ASSERT_EQ(1, diff.added_assignments_size());
  EXPECT_EQ("ship", diff.added_assignments(0).task_id());

  ASSERT_EQ(1, diff.removed_assignments_size());
  EXPECT_EQ("pack", diff.removed_assignments(0).task_id());

  ASSERT_EQ(1, diff.changed_assignments_size());
  EXPECT_EQ("pick", diff.changed_assignments(0).before().task_id());
  EXPECT_EQ("robot_1", diff.changed_assignments(0).before().actor_id());
  EXPECT_EQ("robot_3", diff.changed_assignments(0).after().actor_id());
}

}  // namespace
