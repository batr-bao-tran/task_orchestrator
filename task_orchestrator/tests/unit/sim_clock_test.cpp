#include "utils/sim_clock.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {
namespace to = task_orchestrator;

TEST(SimClockTest, AdvanceToNext) {
  to::SimClock clock;
  EXPECT_EQ(clock.current_time(), 0);

  std::vector<to::SimClock::Time> seen;
  clock.schedule_at(10, [&](to::SimClock::Time t) { seen.push_back(t); });
  clock.schedule_at(5, [&](to::SimClock::Time t) { seen.push_back(t); });
  EXPECT_TRUE(clock.has_pending_events());

  to::SimClock::Time t1 = clock.advance_to_next();
  EXPECT_EQ(t1, 5);
  ASSERT_EQ(seen.size(), 1U);
  EXPECT_EQ(seen[0], 5);

  to::SimClock::Time t2 = clock.advance_to_next();
  EXPECT_EQ(t2, 10);
  EXPECT_EQ(seen.size(), 2U);
  EXPECT_FALSE(clock.has_pending_events());
}

TEST(SimClockTest, RunUntil) {
  to::SimClock clock;
  clock.set_time(0);
  clock.clear_events();
  clock.schedule_at(1, [](to::SimClock::Time) {});
  clock.schedule_at(2, [](to::SimClock::Time) {});
  to::SimClock::Time end = clock.run_until(10);
  EXPECT_EQ(end, 2);
}
}  // namespace
