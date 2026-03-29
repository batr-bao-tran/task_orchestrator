#include <gtest/gtest.h>

#include <vector>

#include "config/config.hpp"
#include "runner/runner.hpp"
#include "utils/sim_clock.hpp"

namespace {
namespace to = task_orchestrator;

to::app::TaskConfig make_task(std::string id, to::Time requested_time, to::Duration duration, to::Time deadline) {
  return to::app::TaskConfig{
      .id = std::move(id),
      .requested_time = requested_time,
      .duration = duration,
      .latest_start_time = 0,
      .deadline = deadline,
      .priority = 0,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
      .phase_durations = {},
  };
}

TEST(DesRunnerTest, AdvanceToNextAndRun) {
  to::SimClock clock;
  std::vector<to::SimClock::Time> events_seen;

  clock.schedule_at(0, [&](to::SimClock::Time t) {
    events_seen.push_back(t);
    to::app::WorkflowConfig cfg;
    cfg.id = "des_wf";
    cfg.actors.push_back({.id = "r1",
                          .type = "robot",
                          .capacity = 1,
                          .windows = {{.start = 0, .end = 100}},
                          .capabilities = {},
                          .execution_cost_per_unit = 0.0});
    auto task = make_task("t1", 0, 10, 50);
    task.allowed_actor_types = {"robot"};
    cfg.tasks.push_back(std::move(task));
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
  cfg2.actors.push_back(to::app::ActorConfig{.id = "r1",
                                             .type = "robot",
                                             .capacity = 1,
                                             .windows = {{.start = 0, .end = 100}},
                                             .capabilities = {},
                                             .execution_cost_per_unit = 0.0});
  cfg2.tasks.push_back(make_task("t1", 0, 10, 50));
  cfg2.tasks.push_back(make_task("t2", 0, 10, 50));
  clock.schedule_at(0, [&](to::SimClock::Time) {
    const to::app::RunResult r = to::app::run(cfg2);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.assignments.size(), 2U);
    EXPECT_FALSE(r.capacity_issue);
    EXPECT_TRUE(r.unfulfilled_task_ids.empty());
  });
  clock.run_until(100);
}
}  // namespace
