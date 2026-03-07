#include <gtest/gtest.h>

#include <vector>

#include "config/config.hpp"
#include "runner/runner.hpp"
#include "task_orchestrator/utils/sim_clock.hpp"

using namespace task_orchestrator::app;
using namespace task_orchestrator;

TEST(DesRunnerTest, AdvanceToNextAndRun) {
  SimClock clock;
  std::vector<Time> events_seen;

  clock.schedule_at(0, [&](Time t) {
    events_seen.push_back(t);
    WorkflowConfig cfg;
    cfg.id = "des_wf";
    cfg.actors.push_back({"r1", "robot", 1, {{0, 100}}});
    cfg.tasks.push_back({"t1", 0, 10, 50, {"robot"}});
    RunResult result = run(cfg);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.assignments.size(), 1u);
  });

  clock.schedule_at(10, [&](Time t) { events_seen.push_back(t); });
  clock.schedule_at(20, [&](Time t) { events_seen.push_back(t); });

  EXPECT_TRUE(clock.current_time() == 0);
  Time t1 = clock.advance_to_next();
  EXPECT_EQ(t1, 0);
  ASSERT_EQ(events_seen.size(), 1u);
  EXPECT_EQ(events_seen[0], 0);

  Time t2 = clock.advance_to_next();
  EXPECT_EQ(t2, 10);
  Time t3 = clock.advance_to_next();
  EXPECT_EQ(t3, 20);
  EXPECT_FALSE(clock.has_pending_events());
}

TEST(DesRunnerTest, RunUntilWithCapacityIssue) {
  SimClock clock;
  clock.clear_events();
  clock.set_time(0);
  WorkflowConfig cfg2;
  cfg2.id = "des_wf2";
  cfg2.actors.push_back({"r1", "robot", 1, {{0, 100}}});
  cfg2.tasks.push_back({"t1", 0, 10, 50, {}});
  cfg2.tasks.push_back({"t2", 0, 10, 50, {}});
  clock.schedule_at(0, [&](Time) {
    RunResult r = run(cfg2);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.assignments.size(), 1u);
    EXPECT_TRUE(r.capacity_issue);
    EXPECT_EQ(r.unfulfilled_task_ids.size(), 1u);
  });
  clock.run_until(100);
}
