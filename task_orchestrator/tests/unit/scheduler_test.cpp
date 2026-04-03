#include "task_orchestrator/core/scheduler.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "task_orchestrator/strategy/edf_strategy.hpp"
#include "task_orchestrator/strategy/fifo_strategy.hpp"

namespace {
namespace to = task_orchestrator;

TEST(SchedulerTest, EmptyWorkflow) {
  to::Workflow w("wf1");
  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, ActorRegistryReturnsSortedIdsAndMutableActors) {
  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "robot_b", .capacity = 1, .availability_windows = {{.start = 0, .end = 10}}, .current_load = 0});
  registry.add(
      to::Actor{.id = "robot_a", .capacity = 1, .availability_windows = {{.start = 0, .end = 10}}, .current_load = 0});

  EXPECT_EQ((std::vector<to::ActorId>{"robot_a", "robot_b"}), registry.actor_ids());
  auto* actor = registry.get_mutable("robot_a");
  ASSERT_NE(nullptr, actor);
  actor->current_load = 1;
  ASSERT_NE(nullptr, registry.get("robot_a"));
  EXPECT_EQ(1, registry.get("robot_a")->current_load);
  EXPECT_EQ(nullptr, registry.get_mutable("missing"));
}

TEST(SchedulerTest, SingleTask) {
  to::Workflow w("wf1");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 1, .deadline = {}});
  to::WorkflowState state;
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P1", result.assignments[0].task_id);
  EXPECT_EQ("A1", result.assignments[0].actor_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
}

TEST(SchedulerTest, PreferHigherPriority) {
  to::Workflow w2("wf2");
  w2.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w2.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  w2.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 10, .deadline = {}});
  to::WorkflowState state;
  to::ActorRegistry reg2;
  reg2.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w2, state, reg2, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ("P2", result.assignments[0].task_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
  EXPECT_EQ("P1", result.assignments[1].task_id);
  EXPECT_EQ(10, result.assignments[1].start_time);
}

TEST(SchedulerTest, AlreadyAssignedTasksSkipped) {
  to::WorkflowState state;
  state.assigned_tasks = {"P1"};
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w3.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w3, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, CompletedTasksSkipped) {
  to::WorkflowState state;
  state.completed_tasks = {"P1"};
  to::Workflow w3("wf3");
  w3.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w3.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w3, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, PhaseDependencies) {
  to::Workflow w4("wf4");
  w4.add_phase(to::Phase{.id = "p1", .name = "P1", .process_ids = {"P1a"}, .dependency_phase_ids = {}});
  w4.add_phase(to::Phase{.id = "p2", .name = "P2", .process_ids = {"P2a"}, .dependency_phase_ids = {"p1"}});
  w4.add_process(to::Process{
      .id = "P1a", .phase_id = "p1", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  w4.add_process(to::Process{
      .id = "P2a", .phase_id = "p2", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = {}});
  to::WorkflowState state4;
  to::ActorRegistry reg4;
  reg4.add(
      to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  const auto result = to::Scheduler::plan(w4, state4, reg4, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P1a", result.assignments[0].task_id);
  state4.completed_phases = {"p1"};
  const auto result2 = to::Scheduler::plan(w4, state4, reg4, 0);
  ASSERT_EQ(1U, result2.assignments.size());
  EXPECT_EQ("P2a", result2.assignments[0].task_id);
}

TEST(SchedulerTest, PlanLazy) {
  to::Workflow w5("wf5");
  w5.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w5.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});
  w5.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});
  to::WorkflowState state5;
  to::ActorRegistry reg5;
  reg5.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  reg5.add(
      to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 1000}}, .current_load = 0});
  size_t count = 0;
  for (const to::Assignment& a : to::Scheduler::plan_lazy(w5, state5, reg5, 0)) {
    EXPECT_TRUE(a.actor_id == "A1" || a.actor_id == "A2") << "actor_id=" << a.actor_id;
    count++;
  }
  EXPECT_EQ(2U, count);
}

struct RankingParam {
  std::string name;
  to::ActorRankingProfile profile;
  std::vector<std::tuple<to::ActorId, to::Time, int>> actor_specs;
  std::unordered_map<to::TaskId, std::unordered_map<to::ActorId, to::Time>> distance;
  to::ActorId expected_actor;
  size_t expected_reason_index;
};

class SchedulerRankingParamTest : public ::testing::TestWithParam<RankingParam> {};

// NOLINTNEXTLINE(misc-override-with-different-visibility)
TEST_P(SchedulerRankingParamTest, SelectsActorByOrderedCriteria) {
  const RankingParam& p = GetParam();
  to::Workflow w("ranking");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 1, .deadline = {}});

  to::ActorRegistry reg;
  for (const auto& [actor_id, window_start, current_load] : p.actor_specs) {
    reg.add(to::Actor{.id = actor_id,
                      .capacity = 2,
                      .availability_windows = {{.start = window_start, .end = 100}},
                      .current_load = current_load});
  }

  to::WorkflowState state;
  state.task_actor_distance = p.distance;

  const auto result = to::Scheduler::plan(w, state, reg, 0, nullptr, &p.profile);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ(p.expected_actor, result.assignments[0].actor_id);
  auto reason_it = result.ranking_decision_criterion_index.find("P1");
  ASSERT_NE(reason_it, result.ranking_decision_criterion_index.end());
  EXPECT_EQ(p.expected_reason_index, reason_it->second);
}

// NOLINTNEXTLINE(modernize-type-traits)
INSTANTIATE_TEST_SUITE_P(RankingCases,
                         SchedulerRankingParamTest,
                         ::testing::Values(
                             RankingParam{
                                 .name = "distance_over_utilization",
                                 .profile = {.criteria = {to::ActorRankingCriterion::DistanceToWork,
                                                          to::ActorRankingCriterion::UptimeUtilisation}},
                                 .actor_specs = {{"A_far_busy", 0, 1}, {"A_near_idle", 0, 0}},
                                 .distance = {{"P1", {{"A_far_busy", 100}, {"A_near_idle", 10}}}},
                                 .expected_actor = "A_near_idle",
                                 .expected_reason_index = 0,
                             },
                             RankingParam{
                                 .name = "distance_tie_use_utilization",
                                 .profile = {.criteria = {to::ActorRankingCriterion::DistanceToWork,
                                                          to::ActorRankingCriterion::UptimeUtilisation}},
                                 .actor_specs = {{"A1", 0, 1}, {"A2", 0, 0}},
                                 .distance = {{"P1", {{"A1", 10}, {"A2", 10}}}},
                                 .expected_actor = "A1",
                                 .expected_reason_index = 0,
                             },
                             RankingParam{
                                 .name = "full_tie_uses_actor_id",
                                 .profile = {.criteria = {to::ActorRankingCriterion::DistanceToWork,
                                                          to::ActorRankingCriterion::EarliestFeasibleStart}},
                                 .actor_specs = {{"B", 0, 0}, {"A", 0, 0}},
                                 .distance = {{"P1", {{"A", 42}, {"B", 42}}}},
                                 .expected_actor = "A",
                                 .expected_reason_index = 0,
                             }),
                         [](const ::testing::TestParamInfo<RankingParam>& info) { return info.param.name; });

TEST(SchedulerTest, RankingSkipsInfeasibleBestAndUsesNext) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{
      .id = "A_best_but_full", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 1});
  reg.add(
      to::Actor{.id = "A_next", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_actor_distance = {{"P1", {{"A_best_but_full", 1}, {"A_next", 100}}}};

  to::ActorRankingProfile profile{
      .criteria = {to::ActorRankingCriterion::DistanceToWork, to::ActorRankingCriterion::EarliestFeasibleStart}};
  const auto result = to::Scheduler::plan(w, state, reg, 0, nullptr, &profile);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("A_next", result.assignments[0].actor_id);
}

TEST(SchedulerTest, RankingCanPreferEarliestCompletionLeastLoadedAndPreferredActor) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(
      to::Actor{.id = "A_fast", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  reg.add(
      to::Actor{.id = "A_slow", .capacity = 1, .availability_windows = {{.start = 3, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_preferred_actors["P1"] = {"A_fast"};

  const to::ActorRankingProfile profile{.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                     to::ActorRankingCriterion::LeastLoaded,
                                                     to::ActorRankingCriterion::PreferredActor}};

  const auto result = to::Scheduler::plan(w, state, reg, 0, nullptr, &profile);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("A_fast", result.assignments[0].actor_id);
  EXPECT_EQ(0U, result.ranking_decision_criterion_index.at("P1"));
}

TEST(SchedulerTest, RankingFallsBackToPreferredActorWhenOtherCriteriaTie) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 2, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  reg.add(to::Actor{.id = "A2", .capacity = 2, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_preferred_actors["P1"] = {"A2"};
  state.actor_load["A1"] = 1;
  state.actor_load["A2"] = 1;

  const to::ActorRankingProfile profile{.criteria = {to::ActorRankingCriterion::EarliestFeasibleCompletion,
                                                     to::ActorRankingCriterion::LeastLoaded,
                                                     to::ActorRankingCriterion::PreferredActor}};

  const auto result = to::Scheduler::plan(w, state, reg, 0, nullptr, &profile);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("A2", result.assignments[0].actor_id);
  EXPECT_EQ(2U, result.ranking_decision_criterion_index.at("P1"));
}

TEST(SchedulerTest, MissingActorAndMissedDeadlineProduceNoAssignment) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = 20});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = 3});

  to::ActorRegistry reg;
  reg.add(
      to::Actor{.id = "A_real", .capacity = 1, .availability_windows = {{.start = 10, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_allowed_actors["P1"] = {"missing_actor"};

  const auto result = to::Scheduler::plan(w, state, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, UnavailableActorsAreExcluded) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  reg.add(to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.unavailable_actors = {"A1"};

  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("A2", result.assignments[0].actor_id);
}

TEST(SchedulerTest, TaskReleaseTimeDelaysStartTime) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 3, .priority = 0, .deadline = 20});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_release_time["P1"] = 7;

  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ(7, result.assignments[0].start_time);
}

TEST(SchedulerTest, TaskAllowedActorsRestrictFeasibleCandidates) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 2, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  reg.add(to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_allowed_actors["P1"] = {"A2"};

  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("A2", result.assignments[0].actor_id);
}

TEST(SchedulerTest, DeadlineInfeasibleTaskIsNotAssigned) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 10, .priority = 0, .deadline = 5});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(w, to::WorkflowState{}, reg, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, PreferredActorCriterionBreaksTies) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 2, .priority = 0, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  reg.add(to::Actor{.id = "A2", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_preferred_actors["P1"] = {"A2"};
  const to::ActorRankingProfile profile{
      .criteria = {to::ActorRankingCriterion::PreferredActor, to::ActorRankingCriterion::EarliestFeasibleStart}};

  const auto result = to::Scheduler::plan(w, state, reg, 0, nullptr, &profile);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("A2", result.assignments[0].actor_id);
}

TEST(SchedulerTest, ResumableTaskCanBeReplannedWhileUnresumableCannot) {
  to::Workflow w("wf");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 10, .deadline = {}});
  w.add_process(to::Process{
      .id = "P2", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 1, .priority = 5, .deadline = {}});

  to::ActorRegistry reg;
  reg.add(to::Actor{.id = "A1", .capacity = 2, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.assigned_tasks = {"P1", "P2"};
  state.resumable_tasks = {"P1"};
  state.unresumable_tasks = {"P2"};

  const auto result = to::Scheduler::plan(w, state, reg, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P1", result.assignments[0].task_id);
}

TEST(SchedulerTest, FifoStrategyOrdersByPhaseThenTaskId) {
  std::vector<to::TaskInfo> tasks = {
      {.id = "task_b",
       .phase_id = "phase_2",
       .duration = 1,
       .priority = 0,
       .release_time = 0,
       .deadline = std::nullopt},
      {.id = "task_c",
       .phase_id = "phase_1",
       .duration = 1,
       .priority = 0,
       .release_time = 0,
       .deadline = std::nullopt},
      {.id = "task_a",
       .phase_id = "phase_1",
       .duration = 1,
       .priority = 0,
       .release_time = 0,
       .deadline = std::nullopt},
  };

  to::FIFOStrategy strategy;
  strategy.order_tasks(tasks);

  ASSERT_EQ(3U, tasks.size());
  EXPECT_EQ("task_a", tasks[0].id);
  EXPECT_EQ("task_c", tasks[1].id);
  EXPECT_EQ("task_b", tasks[2].id);
}

TEST(SchedulerTest, EdfStrategyPrefersTasksWithDeadlinesWhenPriorityTies) {
  std::vector<to::TaskInfo> tasks = {
      {.id = "undated", .phase_id = "phase", .duration = 1, .priority = 5, .release_time = 0, .deadline = std::nullopt},
      {.id = "dated", .phase_id = "phase", .duration = 1, .priority = 5, .release_time = 0, .deadline = 10},
  };

  to::EDFStrategy strategy;
  strategy.order_tasks(tasks);

  ASSERT_EQ(2U, tasks.size());
  EXPECT_EQ("dated", tasks[0].id);
  EXPECT_EQ("undated", tasks[1].id);
}

TEST(SchedulerTest, EarliestFeasibleStartCriterionChoosesSoonestActor) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"task"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "task",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 0,
                                   .deadline = {}});

  to::ActorRegistry registry;
  registry.add(to::Actor{
      .id = "late_actor", .capacity = 1, .availability_windows = {{.start = 5, .end = 100}}, .current_load = 0});
  registry.add(to::Actor{
      .id = "early_actor", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const to::ActorRankingProfile profile{.criteria = {to::ActorRankingCriterion::EarliestFeasibleStart}};
  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0, nullptr, &profile);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("early_actor", result.assignments[0].actor_id);
  EXPECT_EQ(0U, result.ranking_decision_criterion_index.at("task"));
}

TEST(SchedulerTest, LeastLoadedCriterionUsesStateLoadBeforeActorCurrentLoad) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"task"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "task",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 0,
                                   .deadline = {}});

  to::ActorRegistry registry;
  registry.add(to::Actor{
      .id = "busy_actor", .capacity = 3, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  registry.add(to::Actor{
      .id = "light_actor", .capacity = 3, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.actor_load["busy_actor"] = 2;
  state.actor_load["light_actor"] = 0;

  const to::ActorRankingProfile profile{.criteria = {to::ActorRankingCriterion::LeastLoaded}};
  const auto result = to::Scheduler::plan(workflow, state, registry, 0, nullptr, &profile);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("light_actor", result.assignments[0].actor_id);
  EXPECT_EQ(0U, result.ranking_decision_criterion_index.at("task"));
}

TEST(SchedulerTest, PreferredActorCriterionChoosesPreferredCandidateWhenFeasible) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"task"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "task",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 0,
                                   .deadline = {}});

  to::ActorRegistry registry;
  registry.add(to::Actor{
      .id = "non_preferred", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  registry.add(to::Actor{
      .id = "preferred", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  to::WorkflowState state;
  state.task_preferred_actors["task"] = {"preferred"};

  const to::ActorRankingProfile profile{.criteria = {to::ActorRankingCriterion::PreferredActor}};
  const auto result = to::Scheduler::plan(workflow, state, registry, 0, nullptr, &profile);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("preferred", result.assignments[0].actor_id);
  EXPECT_EQ(0U, result.ranking_decision_criterion_index.at("task"));
}

TEST(SchedulerTest, TimedReservationsAllowSequentialReuseOfTheSameActor) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase", .name = "Phase", .process_ids = {"pick", "pack"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "pick",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 4,
                                   .priority = 10,
                                   .deadline = {}});
  workflow.add_process(to::Process{.id = "pack",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 5,
                                   .deadline = {}});

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ("pick", result.assignments[0].task_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
  EXPECT_EQ("pack", result.assignments[1].task_id);
  EXPECT_EQ(4, result.assignments[1].start_time);
}

TEST(SchedulerTest, DependencyTasksAreScheduledAfterTheirPredecessorsComplete) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase", .name = "Phase", .process_ids = {"scan", "lift"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "scan",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 10,
                                   .deadline = 20});
  workflow.add_process(to::Process{.id = "lift",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 5,
                                   .priority = 9,
                                   .deadline = 20,
                                   .dependency_task_ids = {"scan"}});

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());
  EXPECT_EQ("scan", result.assignments[0].task_id);
  EXPECT_EQ(0, result.assignments[0].start_time);
  EXPECT_EQ("lift", result.assignments[1].task_id);
  EXPECT_EQ(3, result.assignments[1].start_time);
}

TEST(SchedulerTest, ConstraintFiltersRespectActorTypeCapabilitiesAndLatestStartTime) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase", .name = "Phase", .process_ids = {"inspect", "handoff"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "inspect",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 10,
                                   .deadline = 10,
                                   .latest_start_time = 2,
                                   .allowed_actor_types = {"robot"},
                                   .required_capabilities = {"scan"}});
  workflow.add_process(to::Process{.id = "handoff",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 5,
                                   .deadline = 10,
                                   .latest_start_time = 1,
                                   .allowed_actor_ids = {"late_robot"},
                                   .required_capabilities = {"scan"}});

  to::ActorRegistry registry;
  registry.add(to::Actor{.id = "scanner_robot",
                         .type = "robot",
                         .capacity = 1,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {"scan"},
                         .execution_cost_per_unit = 0.0,
                         .current_load = 0});
  registry.add(to::Actor{.id = "late_robot",
                         .type = "robot",
                         .capacity = 1,
                         .availability_windows = {{.start = 4, .end = 100}},
                         .capabilities = {"scan"},
                         .execution_cost_per_unit = 0.0,
                         .current_load = 0});
  registry.add(to::Actor{.id = "human_scanner",
                         .type = "human",
                         .capacity = 1,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {"scan"},
                         .execution_cost_per_unit = 0.0,
                         .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("inspect", result.assignments[0].task_id);
  EXPECT_EQ("scanner_robot", result.assignments[0].actor_id);
}

TEST(SchedulerTest, FallbackDistancePrefersKnownActorAndZeroDurationHonorsReleaseTime) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase", .name = "Phase", .process_ids = {"pick", "instant"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "pick",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 10,
                                   .deadline = 20,
                                   .release_time = 0,
                                   .actor_distances = {{"near_robot", 1}}});
  workflow.add_process(to::Process{.id = "instant",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 0,
                                   .priority = 5,
                                   .deadline = 20,
                                   .release_time = 7,
                                   .actor_distances = {{"near_robot", 1}}});

  to::ActorRegistry registry;
  registry.add(to::Actor{
      .id = "near_robot", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  registry.add(to::Actor{
      .id = "far_robot", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const to::ActorRankingProfile profile{
      .criteria = {to::ActorRankingCriterion::DistanceToWork, to::ActorRankingCriterion::EarliestFeasibleStart}};
  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0, nullptr, &profile);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(2U, result.assignments.size());

  const auto pick_it =
      std::ranges::find_if(result.assignments,

                           [](const to::Assignment& assignment) { return assignment.task_id == "pick"; });
  ASSERT_NE(result.assignments.end(), pick_it);
  EXPECT_EQ("near_robot", pick_it->actor_id);

  const auto instant_it = std::ranges::find_if(
      result.assignments, [](const to::Assignment& assignment) { return assignment.task_id == "instant"; });
  ASSERT_NE(result.assignments.end(), instant_it);
  EXPECT_EQ(7, instant_it->start_time);
}

TEST(SchedulerTest, CompletionTimeFallbacksDelayDependentTasks) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase",
                               .name = "Phase",
                               .process_ids = {"done_actual",
                                               "done_planned",
                                               "done_now",
                                               "assigned_planned",
                                               "assigned_default",
                                               "after_actual",
                                               "after_planned",
                                               "after_now",
                                               "after_assigned_planned",
                                               "after_assigned_default"},
                               .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "done_actual",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 0,
                                   .deadline = {}});
  workflow.add_process(to::Process{.id = "done_planned",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 0,
                                   .deadline = {}});
  workflow.add_process(to::Process{.id = "done_now",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 0,
                                   .deadline = {}});
  workflow.add_process(to::Process{.id = "assigned_planned",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 0,
                                   .deadline = {}});
  workflow.add_process(to::Process{.id = "assigned_default",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 4,
                                   .priority = 0,
                                   .deadline = {}});
  workflow.add_process(to::Process{.id = "after_actual",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 1,
                                   .priority = 9,
                                   .deadline = {},
                                   .dependency_task_ids = {"done_actual"}});
  workflow.add_process(to::Process{.id = "after_planned",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 1,
                                   .priority = 8,
                                   .deadline = {},
                                   .dependency_task_ids = {"done_planned"}});
  workflow.add_process(to::Process{.id = "after_now",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 1,
                                   .priority = 7,
                                   .deadline = {},
                                   .dependency_task_ids = {"done_now"}});
  workflow.add_process(to::Process{.id = "after_assigned_planned",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 1,
                                   .priority = 6,
                                   .deadline = {},
                                   .dependency_task_ids = {"assigned_planned"}});
  workflow.add_process(to::Process{.id = "after_assigned_default",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 1,
                                   .priority = 5,
                                   .deadline = {},
                                   .dependency_task_ids = {"assigned_default"}});

  to::WorkflowState state;
  state.completed_tasks = {"done_actual", "done_planned", "done_now"};
  state.assigned_tasks = {"assigned_planned", "assigned_default"};
  state.task_actual_completion_time["done_actual"] = 14;
  state.task_planned_end_time["done_planned"] = 12;
  state.task_planned_start_time["assigned_planned"] = 8;
  state.task_planned_end_time["assigned_planned"] = 11;

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "robot_1", .capacity = 5, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, state, registry, 5);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(5U, result.assignments.size());

  auto start_time_for = [&result](const char* task_id) -> std::optional<to::Time> {
    const auto assignment_it = std::ranges::find_if(
        result.assignments, [task_id](const to::Assignment& assignment) { return assignment.task_id == task_id; });
    if (assignment_it == result.assignments.end()) {
      return std::nullopt;
    }
    return assignment_it->start_time;
  };

  EXPECT_EQ(std::optional<to::Time>(14), start_time_for("after_actual"));
  EXPECT_EQ(std::optional<to::Time>(12), start_time_for("after_planned"));
  EXPECT_EQ(std::optional<to::Time>(5), start_time_for("after_now"));
  EXPECT_EQ(std::optional<to::Time>(11), start_time_for("after_assigned_planned"));
  EXPECT_EQ(std::optional<to::Time>(9), start_time_for("after_assigned_default"));
}

TEST(SchedulerTest, UnsatisfiedDependenciesProduceNoReadyTasks) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"blocked"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "blocked",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 1,
                                   .priority = 1,
                                   .deadline = {},
                                   .dependency_task_ids = {"never_completed"}});

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
}

TEST(SchedulerTest, MutualExclusionPreventsConflictingTasksFromBeingPlannedTogether) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase", .name = "Phase", .process_ids = {"critical", "fallback"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "critical",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 10,
                                   .deadline = 20,
                                   .mutually_exclusive_task_ids = {"fallback"}});
  workflow.add_process(to::Process{.id = "fallback",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 3,
                                   .priority = 1,
                                   .deadline = 20,
                                   .mutually_exclusive_task_ids = {"critical"}});

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("critical", result.assignments[0].task_id);
}

TEST(SchedulerTest, DemandConstraintUsesHigherCapacityActorAndExecutionCostRanking) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"bulk"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "bulk",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 4,
                                   .priority = 10,
                                   .deadline = 20,
                                   .demand = 2});

  to::ActorRegistry registry;
  registry.add(to::Actor{.id = "expensive_big",
                         .type = "robot",
                         .capacity = 2,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {},
                         .execution_cost_per_unit = 5.0,
                         .current_load = 0});
  registry.add(to::Actor{.id = "cheap_big",
                         .type = "robot",
                         .capacity = 2,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {},
                         .execution_cost_per_unit = 1.0,
                         .current_load = 0});
  registry.add(to::Actor{.id = "small",
                         .type = "robot",
                         .capacity = 1,
                         .availability_windows = {{.start = 0, .end = 100}},
                         .capabilities = {},
                         .execution_cost_per_unit = 0.5,
                         .current_load = 0});

  const to::ActorRankingProfile profile{
      .criteria = {to::ActorRankingCriterion::ExecutionCost, to::ActorRankingCriterion::EarliestFeasibleCompletion}};
  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0, nullptr, &profile);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("cheap_big", result.assignments[0].actor_id);
}

TEST(SchedulerTest, PreemptibleTasksReportUnsupportedSchedulerConstraint) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"task"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "task",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 1,
                                   .deadline = 10,
                                   .preemptible = true});

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, to::WorkflowState{}, registry, 0);
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.assignments.empty());
  EXPECT_NE(std::string::npos, result.error_message.find("Preemptible tasks"));
}

TEST(SchedulerTest, ResumableTaskStateCanBePlannedAgainAfterFailure) {
  to::Workflow workflow("wf");
  workflow.add_phase(to::Phase{.id = "phase", .name = "Phase", .process_ids = {"task"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{.id = "task",
                                   .phase_id = "phase",
                                   .sub_process_ids = {},
                                   .estimated_duration = 2,
                                   .priority = 1,
                                   .deadline = {}});

  to::WorkflowState state;
  state.resumable_tasks = {"task"};
  state.task_planned_start_time["task"] = 0;
  state.task_planned_end_time["task"] = 2;

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, state, registry, 3);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("task", result.assignments[0].task_id);
  EXPECT_EQ(3, result.assignments[0].start_time);
}

TEST(SchedulerTest, PendingSiblingTaskCanBePlannedAfterAnotherTaskCompletes) {
  to::Workflow workflow("wf");
  workflow.add_phase(
      to::Phase{.id = "phase", .name = "Phase", .process_ids = {"P1", "P2"}, .dependency_phase_ids = {}});
  workflow.add_process(to::Process{
      .id = "P1", .phase_id = "phase", .sub_process_ids = {}, .estimated_duration = 5, .priority = 10, .deadline = {}});
  workflow.add_process(to::Process{
      .id = "P2", .phase_id = "phase", .sub_process_ids = {}, .estimated_duration = 4, .priority = 1, .deadline = {}});

  to::WorkflowState state;
  state.completed_tasks = {"P1"};
  state.task_actual_completion_time["P1"] = 5;
  state.task_planned_start_time["P1"] = 0;
  state.task_planned_end_time["P1"] = 5;

  to::ActorRegistry registry;
  registry.add(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});

  const auto result = to::Scheduler::plan(workflow, state, registry, 5);
  ASSERT_TRUE(result.ok);
  ASSERT_EQ(1U, result.assignments.size());
  EXPECT_EQ("P2", result.assignments[0].task_id);
  EXPECT_EQ(5, result.assignments[0].start_time);
}
}  // namespace
