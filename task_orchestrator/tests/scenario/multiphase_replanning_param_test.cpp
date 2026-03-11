#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace {
namespace to = task_orchestrator;

enum class StepType {
  Tick,
  FailResumable,
  FailUnresumable,
  MarkResumable,
  SetA1Unavailable,
  SetA1Available,
};

struct ScenarioStep {
  StepType type;
  to::Time at;
};

struct MultiPhaseScenarioParam {
  std::string name;
  std::vector<ScenarioStep> steps_before_phase1_complete;
  bool expect_p1_replanned_before_phase1_complete;
  bool expect_p1_replanned_to_a2_before_phase1_complete;
};

void setup_multiphase_orchestrator(to::Orchestrator& o) {
  to::Workflow w("wf_multi");
  w.add_phase(to::Phase{.id = "phase1", .name = "Phase 1", .process_ids = {"P1A"}, .dependency_phase_ids = {}});
  w.add_phase(to::Phase{.id = "phase2", .name = "Phase 2", .process_ids = {"P2A"}, .dependency_phase_ids = {"phase1"}});
  w.add_phase(to::Phase{.id = "phase3", .name = "Phase 3", .process_ids = {"P3A"}, .dependency_phase_ids = {"phase2"}});

  w.add_process(to::Process{.id = "P1A",
                            .phase_id = "phase1",
                            .sub_process_ids = {},
                            .estimated_duration = 3,
                            .priority = 10,
                            .deadline = {}});
  w.add_process(to::Process{.id = "P2A",
                            .phase_id = "phase2",
                            .sub_process_ids = {},
                            .estimated_duration = 3,
                            .priority = 5,
                            .deadline = {}});
  w.add_process(to::Process{.id = "P3A",
                            .phase_id = "phase3",
                            .sub_process_ids = {},
                            .estimated_duration = 3,
                            .priority = 1,
                            .deadline = {}});

  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(
      to::Actor{.id = "A2", .capacity = 4, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.start();
  o.tick(0);  // dispatch initial plan
}

void trigger_replan_without_capacity_impact(to::Orchestrator& o) {
  o.set_actor_unavailable("__replan_marker__", true);
  o.set_actor_unavailable("__replan_marker__", false);
}

class MultiPhaseReplanningParamTest : public ::testing::TestWithParam<MultiPhaseScenarioParam> {};

// NOLINTNEXTLINE(misc-override-with-different-visibility)
TEST_P(MultiPhaseReplanningParamTest, HandlesFailureAndAvailabilityOrderingAcrossPhases) {
  const MultiPhaseScenarioParam& param = GetParam();
  to::Orchestrator o;
  setup_multiphase_orchestrator(o);

  bool saw_p1_replanned = false;
  bool saw_p1_replanned_to_a2 = false;
  bool saw_phase2_before_phase1_complete = false;
  bool p1_failed = false;

  for (const ScenarioStep& step : param.steps_before_phase1_complete) {
    switch (step.type) {
      case StepType::Tick:
        o.tick(step.at);
        o.tick(step.at + 1);  // apply dispatch if a schedule was produced
        break;
      case StepType::FailResumable:
        o.notify_task_failed("P1A", true);
        p1_failed = true;
        break;
      case StepType::FailUnresumable:
        o.notify_task_failed("P1A", false);
        p1_failed = true;
        break;
      case StepType::MarkResumable:
        o.mark_task_resumable("P1A");
        break;
      case StepType::SetA1Unavailable:
        o.set_actor_unavailable("A1", true);
        break;
      case StepType::SetA1Available:
        o.set_actor_unavailable("A1", false);
        break;
    }
    const auto* state = o.workflow_state();
    ASSERT_NE(nullptr, state);
    if (p1_failed) {
      auto p1_it = state->task_actor.find("P1A");
      if (p1_it != state->task_actor.end()) {
        saw_p1_replanned = true;
        if (p1_it->second == "A2") {
          saw_p1_replanned_to_a2 = true;
        }
      }
    }
    if (state->task_actor.contains("P2A")) {
      saw_phase2_before_phase1_complete = true;
    }
  }

  EXPECT_EQ(param.expect_p1_replanned_before_phase1_complete, saw_p1_replanned);
  EXPECT_EQ(param.expect_p1_replanned_to_a2_before_phase1_complete, saw_p1_replanned_to_a2);
  EXPECT_FALSE(saw_phase2_before_phase1_complete);

  o.complete_phase("phase1");
  trigger_replan_without_capacity_impact(o);
  o.tick(100);
  o.tick(101);
  const auto* state_after_phase2 = o.workflow_state();
  ASSERT_NE(nullptr, state_after_phase2);
  EXPECT_TRUE(!state_after_phase2->completed_phases.empty());

  o.complete_phase("phase2");
  trigger_replan_without_capacity_impact(o);
  o.tick(200);
  o.tick(201);
  const auto* state_after_phase3 = o.workflow_state();
  ASSERT_NE(nullptr, state_after_phase3);
  EXPECT_TRUE(state_after_phase3->completed_phases.size() >= 2U);
}

// NOLINTNEXTLINE(modernize-type-traits)
INSTANTIATE_TEST_SUITE_P(OrderingMatrix,
                         MultiPhaseReplanningParamTest,
                         ::testing::Values(
                             MultiPhaseScenarioParam{
                                 .name = "fail_resumable_then_tick",
                                 .steps_before_phase1_complete =
                                     {
                                         {StepType::FailResumable, 1},
                                         {StepType::Tick, 2},
                                     },
                                 .expect_p1_replanned_before_phase1_complete = true,
                                 .expect_p1_replanned_to_a2_before_phase1_complete = false,
                             },
                             MultiPhaseScenarioParam{
                                 .name = "actor_unavailable_then_fail_resumable_then_tick",
                                 .steps_before_phase1_complete =
                                     {
                                         {StepType::SetA1Unavailable, 1},
                                         {StepType::FailResumable, 2},
                                         {StepType::Tick, 3},
                                     },
                                 .expect_p1_replanned_before_phase1_complete = true,
                                 .expect_p1_replanned_to_a2_before_phase1_complete = true,
                             },
                             MultiPhaseScenarioParam{
                                 .name = "fail_resumable_then_actor_unavailable_then_tick",
                                 .steps_before_phase1_complete =
                                     {
                                         {StepType::FailResumable, 1},
                                         {StepType::SetA1Unavailable, 2},
                                         {StepType::Tick, 3},
                                     },
                                 .expect_p1_replanned_before_phase1_complete = true,
                                 .expect_p1_replanned_to_a2_before_phase1_complete = true,
                             },
                             MultiPhaseScenarioParam{
                                 .name = "fail_unresumable_then_tick_no_replan",
                                 .steps_before_phase1_complete =
                                     {
                                         {StepType::FailUnresumable, 1},
                                         {StepType::Tick, 2},
                                     },
                                 .expect_p1_replanned_before_phase1_complete = false,
                                 .expect_p1_replanned_to_a2_before_phase1_complete = false,
                             },
                             MultiPhaseScenarioParam{
                                 .name = "fail_unresumable_then_mark_resumable_then_tick",
                                 .steps_before_phase1_complete =
                                     {
                                         {StepType::FailUnresumable, 1},
                                         {StepType::MarkResumable, 2},
                                         {StepType::Tick, 3},
                                     },
                                 .expect_p1_replanned_before_phase1_complete = true,
                                 .expect_p1_replanned_to_a2_before_phase1_complete = false,
                             },
                             MultiPhaseScenarioParam{
                                 .name = "fail_unresumable_mark_resumable_a1_unavailable_then_tick",
                                 .steps_before_phase1_complete =
                                     {
                                         {StepType::FailUnresumable, 1},
                                         {StepType::MarkResumable, 2},
                                         {StepType::SetA1Unavailable, 3},
                                         {StepType::Tick, 4},
                                     },
                                 .expect_p1_replanned_before_phase1_complete = true,
                                 .expect_p1_replanned_to_a2_before_phase1_complete = true,
                             }),
                         [](const ::testing::TestParamInfo<MultiPhaseScenarioParam>& info) { return info.param.name; });

}  // namespace
