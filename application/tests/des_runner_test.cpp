#include <gtest/gtest.h>

#include <vector>

#include "config/config.hpp"
#include "runner/runner.hpp"
#include "utils/sim_clock.hpp"

namespace {
namespace to = task_orchestrator;

TEST(DesRunnerTest, AdvanceToNextAndRun) {
  to::SimClock clock;
  std::vector<to::SimClock::Time> events_seen;

  clock.schedule_at(0, [&](to::SimClock::Time t) {
    events_seen.push_back(t);
    to::app::WorkflowConfig cfg;
    cfg.id = "des_wf";
    cfg.actors.push_back({.id = "r1", .type = "robot", .capacity = 1, .windows = {{.start = 0, .end = 100}}});
    cfg.tasks.push_back(to::app::TaskConfig{.id = "t1",
                                            .requested_time = 0,
                                            .duration = 10,
                                            .deadline = 50,
                                            .allowed_actor_types = {"robot"},
                                            .phase_durations = {}});
    const to::app::RunResult result = to::app::run(cfg);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.assignments.size(), 1U);
  });

  clock.schedule_at(10, [&](to::SimClock::Time t) { events_seen.push_back(t); });
  clock.schedule_at(20, [&](to::SimClock::Time t) { events_seen.push_back(t); });

  EXPECT_TRUE(clock.current_time() == 0);
  const to::SimClock::Time t1 = clock.advance_to_next();
  EXPECT_EQ(t1, 0);
  ASSERT_EQ(events_seen.size(), 1U);
  EXPECT_EQ(events_seen[0], 0);

  const to::SimClock::Time t2 = clock.advance_to_next();
  EXPECT_EQ(t2, 10);
  const to::SimClock::Time t3 = clock.advance_to_next();
  EXPECT_EQ(t3, 20);
  EXPECT_FALSE(clock.has_pending_events());
}

TEST(DesRunnerTest, RunUntilWithCapacityIssue) {
  to::SimClock clock;
  clock.clear_events();
  clock.set_time(0);
  to::app::WorkflowConfig cfg2;
  cfg2.id = "des_wf2";
  cfg2.actors.push_back(
      to::app::ActorConfig{.id = "r1", .type = "robot", .capacity = 1, .windows = {{.start = 0, .end = 100}}});
  cfg2.tasks.push_back(to::app::TaskConfig{.id = "t1",
                                           .requested_time = 0,
                                           .duration = 10,
                                           .deadline = 50,
                                           .allowed_actor_types = {},
                                           .phase_durations = {}});
  cfg2.tasks.push_back(to::app::TaskConfig{.id = "t2",
                                           .requested_time = 0,
                                           .duration = 10,
                                           .deadline = 50,
                                           .allowed_actor_types = {},
                                           .phase_durations = {}});
  clock.schedule_at(0, [&](to::SimClock::Time) {
    const to::app::RunResult r = to::app::run(cfg2);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.assignments.size(), 1U);
    EXPECT_TRUE(r.capacity_issue);
    EXPECT_EQ(r.unfulfilled_task_ids.size(), 1U);
  });
  clock.run_until(100);
}
}  // namespace
