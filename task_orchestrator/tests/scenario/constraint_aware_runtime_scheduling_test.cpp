#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

to::Workflow make_constraint_aware_workflow() {
  to::Workflow workflow("constraint_aware_runtime");
  workflow.add_phase(to::Phase{
      .id = "ops", .name = "Operations", .process_ids = {"scan", "bulk_lift", "fallback"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "scan",
                                   .phase_id = "ops",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 10,
                                   .deadline = 12,
                                   .allowed_actor_types = {"robot"},
                                   .preferred_actor_ids = {"scan_bot"},
                                   .required_capabilities = {"scan"},
                                   .actor_distances = {{"scan_bot", 1}, {"hybrid_bot", 4}}});
  workflow.add_process(to::Process{.id = "bulk_lift",
                                   .phase_id = "ops",
                                   .sub_process_ids = {},
                                   .estimated_duration = 4,
                                   .priority = 9,
                                   .deadline = 20,
                                   .demand = 2,
                                   .allowed_actor_ids = {"hybrid_bot"},
                                   .required_capabilities = {"lift"},
                                   .dependency_task_ids = {"scan"}});
  workflow.add_process(to::Process{.id = "fallback",
                                   .phase_id = "ops",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 1,
                                   .deadline = 20,
                                   .allowed_actor_types = {"robot"},
                                   .required_capabilities = {"lift"},
                                   .dependency_task_ids = {"scan"},
                                   .mutually_exclusive_task_ids = {"bulk_lift"}});
  return workflow;
}

to::ActorRegistry make_constraint_aware_registry() {
  to::ActorRegistry registry;
  registry.add(to::Actor{.id = "scan_bot",
                         .type = "robot",
                         .capacity = 1,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {"scan"},
                         .execution_cost_per_unit = 1.0,
                         .current_load = 0});
  registry.add(to::Actor{.id = "hybrid_bot",
                         .type = "robot",
                         .capacity = 2,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {"scan", "lift"},
                         .execution_cost_per_unit = 2.0,
                         .current_load = 0});
  registry.add(to::Actor{.id = "human_scan",
                         .type = "human",
                         .capacity = 1,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {"scan"},
                         .execution_cost_per_unit = 0.5,
                         .current_load = 0});
  return registry;
}

TEST(ConstraintAwareRuntimeSchedulingTest, SchedulerProducesOptimizerStyleFeasiblePlan) {
  const to::Workflow workflow = make_constraint_aware_workflow();
  const to::ActorRegistry registry = make_constraint_aware_registry();
  const to::ActorRankingProfile profile{.criteria = {to::ActorRankingCriterion::PreferredActor,
                                                     to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                     to::ActorRankingCriterion::ExecutionCost}};

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0, nullptr, &profile);
  ASSERT_TRUE(result.ok) << result.error_message;
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ("scan", result.assignments[0].task_id);
  EXPECT_EQ("scan_bot", result.assignments[0].actor_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
  EXPECT_EQ("bulk_lift", result.assignments[1].task_id);
  EXPECT_EQ("hybrid_bot", result.assignments[1].actor_id);
  EXPECT_EQ(3, result.assignments[1].start_time);
}

TEST(ConstraintAwareRuntimeSchedulingTest, OrchestratorWaitsForActualDependencyCompletionBeforeDispatchingFutureWork) {
  to::Orchestrator orchestrator;
  orchestrator.set_workflow(make_constraint_aware_workflow());
  const to::ActorRegistry registry = make_constraint_aware_registry();
  for (const to::ActorId& actor_id : registry.actor_ids()) {
    const to::Actor* actor = registry.get(actor_id);
    ASSERT_NE(nullptr, actor);
    orchestrator.register_actor(*actor);
  }

  orchestrator.start();
  orchestrator.tick(0);
  const auto initial_schedule = orchestrator.get_latest_schedule();
  ASSERT_TRUE(initial_schedule.ok);
  ASSERT_EQ(2U, initial_schedule.assignments.size());
  EXPECT_EQ("scan", initial_schedule.assignments[0].task_id);
  EXPECT_EQ("bulk_lift", initial_schedule.assignments[1].task_id);
  EXPECT_EQ(3, initial_schedule.assignments[1].start_time);

  const auto* state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_EQ(std::vector<to::TaskId>({"scan"}), state->assigned_tasks);
  EXPECT_FALSE(state->task_actor.contains("bulk_lift"));

  orchestrator.tick(3);
  state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_EQ(std::vector<to::TaskId>({"scan"}), state->assigned_tasks);
  EXPECT_FALSE(state->task_actor.contains("bulk_lift"));

  orchestrator.notify_task_completed("scan", 4);
  orchestrator.tick(4);
  const auto replanned = orchestrator.get_latest_schedule();
  ASSERT_TRUE(replanned.ok);
  ASSERT_EQ(1U, replanned.assignments.size());
  EXPECT_EQ("bulk_lift", replanned.assignments[0].task_id);
  EXPECT_EQ(4, replanned.assignments[0].start_time);

  orchestrator.tick(5);
  state = orchestrator.workflow_state();
  ASSERT_NE(nullptr, state);
  EXPECT_TRUE(std::ranges::find(state->assigned_tasks, "bulk_lift") != state->assigned_tasks.end());
  EXPECT_EQ("hybrid_bot", state->task_actor.at("bulk_lift"));
  EXPECT_EQ(2, state->actor_load.at("hybrid_bot"));
}
}  // namespace
