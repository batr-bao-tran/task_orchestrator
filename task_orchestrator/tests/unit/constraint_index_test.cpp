#include "task_orchestrator/optimizer/constraint_index.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

namespace {
namespace too = task_orchestrator::optimizer;

too::OptimizationModel make_model() {
  too::OptimizationModel model;
  model.id = "constraint_index";
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_1",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_2",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });
  model.actors.push_back(too::OptimizerActor{
      .id = "machine_1",
      .type = "machine",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });

  model.tasks.push_back(too::OptimizerTask{
      .id = "pick",
      .duration = 1,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "pack",
      .duration = 1,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {},
      .allowed_actor_ids = {"robot_2", "machine_1"},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {"pick"},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "ship",
      .duration = 1,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {"pack"},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });
  return model;
}

TEST(ConstraintIndexTest, ResolvesEntitiesAndEligibleActors) {
  const too::OptimizationModel model = make_model();
  const too::ConstraintIndex index(model);

  ASSERT_NE(nullptr, index.task("pick"));
  ASSERT_NE(nullptr, index.actor("robot_1"));
  EXPECT_EQ(nullptr, index.task("missing_task"));
  EXPECT_EQ(nullptr, index.actor("missing_actor"));

  const auto eligible_by_type = index.eligible_actors_for_task(*index.task("pick"));
  ASSERT_EQ(2U, eligible_by_type.size());
  EXPECT_EQ("robot_1", eligible_by_type[0]->id);
  EXPECT_EQ("robot_2", eligible_by_type[1]->id);

  const auto eligible_by_id = index.eligible_actors_for_task(*index.task("pack"));
  ASSERT_EQ(2U, eligible_by_id.size());
  EXPECT_EQ("robot_2", eligible_by_id[0]->id);
  EXPECT_EQ("machine_1", eligible_by_id[1]->id);
}

TEST(ConstraintIndexTest, TracksSuccessorsAndDependencyState) {
  const too::OptimizationModel model = make_model();
  const too::ConstraintIndex index(model);

  const auto& successors = index.successors("pick");
  ASSERT_EQ(1U, successors.size());
  EXPECT_EQ("pack", successors[0]);
  EXPECT_TRUE(index.successors("missing").empty());

  const too::OptimizerTask* pack = index.task("pack");
  ASSERT_NE(nullptr, pack);

  std::unordered_set<task_orchestrator::TaskId> scheduled = {"pick"};
  std::unordered_set<task_orchestrator::TaskId> dropped;
  EXPECT_TRUE(index.dependencies_satisfied(*pack, scheduled, dropped));
  EXPECT_FALSE(index.dependency_blocked(*pack, dropped));

  dropped.insert("pick");
  EXPECT_FALSE(index.dependencies_satisfied(*pack, scheduled, dropped));
  EXPECT_TRUE(index.dependency_blocked(*pack, dropped));
}

TEST(ConstraintIndexTest, UnrestrictedTasksSeeAllActors) {
  const too::OptimizationModel model = make_model();
  const too::ConstraintIndex index(model);

  const auto eligible = index.eligible_actors_for_task(*index.task("ship"));
  ASSERT_EQ(3U, eligible.size());
  EXPECT_EQ("robot_1", eligible[0]->id);
  EXPECT_EQ("robot_2", eligible[1]->id);
  EXPECT_EQ("machine_1", eligible[2]->id);
}

TEST(ConstraintIndexTest, AllowedActorTypesFilterAllowedActorIds) {
  too::OptimizationModel model = make_model();
  model.tasks.push_back(too::OptimizerTask{
      .id = "inspect",
      .duration = 1,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {"machine_1", "robot_1"},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  const too::ConstraintIndex index(model);
  const auto eligible = index.eligible_actors_for_task(*index.task("inspect"));
  ASSERT_EQ(1U, eligible.size());
  EXPECT_EQ("robot_1", eligible.front()->id);
}

}  // namespace
