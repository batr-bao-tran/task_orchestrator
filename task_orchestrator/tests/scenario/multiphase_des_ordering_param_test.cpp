#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/orchestrator.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "utils/sim_clock.hpp"

namespace {
namespace to = task_orchestrator;

enum class EventType {
  FailResumableP1,
  FailUnresumableP1,
  MarkP1Resumable,
  A1Unavailable,
  CompletePhase1,
  CompletePhase2,
  TriggerReplan,
  Tick,
};

struct TimedEvent {
  to::Time at;
  EventType event;
};

struct DesOrderingParam {
  std::string name;
  std::vector<TimedEvent> events;
  bool expect_p1_replanned;
  bool expect_no_p1_until_resumable;
};

void setup_multiphase_orchestrator(to::Orchestrator& o) {
  to::Workflow w("wf_multi_des");
  w.add_phase(to::Phase{.id = "phase1", .name = "Phase 1", .process_ids = {"P1A"}, .dependency_phase_ids = {}});
  w.add_phase(to::Phase{.id = "phase2", .name = "Phase 2", .process_ids = {"P2A"}, .dependency_phase_ids = {"phase1"}});
  w.add_phase(to::Phase{.id = "phase3", .name = "Phase 3", .process_ids = {"P3A"}, .dependency_phase_ids = {"phase2"}});
  w.add_process(to::Process{.id = "P1A",
                            .phase_id = "phase1",
                            .sub_process_ids = {},
                            .estimated_duration = 2,
                            .priority = 10,
                            .deadline = {}});
  w.add_process(to::Process{.id = "P2A",
                            .phase_id = "phase2",
                            .sub_process_ids = {},
                            .estimated_duration = 2,
                            .priority = 5,
                            .deadline = {}});
  w.add_process(to::Process{.id = "P3A",
                            .phase_id = "phase3",
                            .sub_process_ids = {},
                            .estimated_duration = 2,
                            .priority = 1,
                            .deadline = {}});

  o.set_workflow(std::move(w));
  o.register_actor(
      to::Actor{.id = "A1", .capacity = 1, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.register_actor(
      to::Actor{.id = "A2", .capacity = 4, .availability_windows = {{.start = 0, .end = 100}}, .current_load = 0});
  o.start();
  o.tick(0);
}

class MultiPhaseDesOrderingParamTest : public ::testing::TestWithParam<DesOrderingParam> {};

// NOLINTNEXTLINE(misc-override-with-different-visibility)
TEST_P(MultiPhaseDesOrderingParamTest, HandlesEventOrderingAcrossFailureAvailabilityAndPhases) {
  const DesOrderingParam& param = GetParam();
  to::Orchestrator o;
  setup_multiphase_orchestrator(o);

  std::unordered_map<to::Time, std::vector<std::string>> tasks_seen_by_tick;
  bool saw_p1_before_resumable = false;
  bool resumable_marked = false;
  bool p1_failed = false;
  bool saw_p1_replanned = false;

  to::SimClock clock;
  for (const TimedEvent& e : param.events) {
    const TimedEvent event = e;
    clock.schedule_at(event.at,
                      [&o,
                       &tasks_seen_by_tick,
                       &saw_p1_before_resumable,
                       &resumable_marked,
                       &p1_failed,
                       &saw_p1_replanned,
                       scheduled_event = event](to::SimClock::Time t) {
                        switch (scheduled_event.event) {
                          case EventType::FailResumableP1:
                            o.notify_task_failed("P1A", true);
                            p1_failed = true;
                            resumable_marked = true;
                            break;
                          case EventType::FailUnresumableP1:
                            o.notify_task_failed("P1A", false);
                            p1_failed = true;
                            break;
                          case EventType::MarkP1Resumable:
                            o.mark_task_resumable("P1A");
                            resumable_marked = true;
                            break;
                          case EventType::A1Unavailable:
                            o.set_actor_unavailable("A1", true);
                            break;
                          case EventType::CompletePhase1:
                            o.complete_phase("phase1");
                            break;
                          case EventType::CompletePhase2:
                            o.complete_phase("phase2");
                            break;
                          case EventType::TriggerReplan:
                            o.set_actor_unavailable("__replan_marker__", true);
                            o.set_actor_unavailable("__replan_marker__", false);
                            break;
                          case EventType::Tick: {
                            o.tick(t);
                            o.tick(t + 1);  // dispatch if planning happened
                            const auto* state = o.workflow_state();
                            ASSERT_NE(nullptr, state);
                            for (const auto& [task_id, actor_id] : state->task_actor) {
                              std::string entry = task_id;
                              entry += "@";
                              entry += actor_id;
                              tasks_seen_by_tick[t].push_back(std::move(entry));
                              if (p1_failed && task_id == "P1A" && !resumable_marked) {
                                saw_p1_before_resumable = true;
                              }
                              if (p1_failed && task_id == "P1A") {
                                saw_p1_replanned = true;
                              }
                            }
                            break;
                          }
                        }
                      });
  }
  clock.run_until(1000);

  EXPECT_EQ(param.expect_p1_replanned, saw_p1_replanned);
  EXPECT_EQ(param.expect_no_p1_until_resumable, !saw_p1_before_resumable);
}

// NOLINTNEXTLINE(modernize-type-traits) -- gtest INSTANTIATE_TEST_SUITE_P macro
INSTANTIATE_TEST_SUITE_P(DesOrderingMatrix,
                         MultiPhaseDesOrderingParamTest,
                         ::testing::Values(
                             DesOrderingParam{
                                 .name = "fail_resumable_then_unavailable_then_tick_then_progress",
                                 .events =
                                     {
                                         {10, EventType::FailResumableP1},
                                         {11, EventType::A1Unavailable},
                                         {12, EventType::Tick},
                                         {20, EventType::CompletePhase1},
                                         {21, EventType::TriggerReplan},
                                         {22, EventType::Tick},
                                         {30, EventType::CompletePhase2},
                                         {31, EventType::TriggerReplan},
                                         {32, EventType::Tick},
                                     },
                                 .expect_p1_replanned = true,
                                 .expect_no_p1_until_resumable = true,
                             },
                             DesOrderingParam{
                                 .name = "unavailable_then_fail_resumable_then_tick_then_progress",
                                 .events =
                                     {
                                         {10, EventType::A1Unavailable},
                                         {11, EventType::FailResumableP1},
                                         {12, EventType::Tick},
                                         {20, EventType::CompletePhase1},
                                         {21, EventType::TriggerReplan},
                                         {22, EventType::Tick},
                                         {30, EventType::CompletePhase2},
                                         {31, EventType::TriggerReplan},
                                         {32, EventType::Tick},
                                     },
                                 .expect_p1_replanned = true,
                                 .expect_no_p1_until_resumable = true,
                             },
                             DesOrderingParam{
                                 .name = "fail_unresumable_tick_mark_resumable_tick_then_progress",
                                 .events =
                                     {
                                         {10, EventType::FailUnresumableP1},
                                         {11, EventType::Tick},
                                         {12, EventType::MarkP1Resumable},
                                         {13, EventType::Tick},
                                         {20, EventType::CompletePhase1},
                                         {21, EventType::TriggerReplan},
                                         {22, EventType::Tick},
                                         {30, EventType::CompletePhase2},
                                         {31, EventType::TriggerReplan},
                                         {32, EventType::Tick},
                                     },
                                 .expect_p1_replanned = true,
                                 .expect_no_p1_until_resumable = true,
                             },
                             DesOrderingParam{
                                 .name = "fail_unresumable_mark_resumable_unavailable_then_tick_then_progress",
                                 .events =
                                     {
                                         {10, EventType::FailUnresumableP1},
                                         {11, EventType::MarkP1Resumable},
                                         {12, EventType::A1Unavailable},
                                         {13, EventType::Tick},
                                         {20, EventType::CompletePhase1},
                                         {21, EventType::TriggerReplan},
                                         {22, EventType::Tick},
                                         {30, EventType::CompletePhase2},
                                         {31, EventType::TriggerReplan},
                                         {32, EventType::Tick},
                                     },
                                 .expect_p1_replanned = true,
                                 .expect_no_p1_until_resumable = true,
                             }),
                         [](const ::testing::TestParamInfo<DesOrderingParam>& info) { return info.param.name; });

}  // namespace
