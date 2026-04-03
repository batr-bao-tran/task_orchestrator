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
  EXPECT_EQ(end, 10);
  EXPECT_EQ(clock.current_time(), 10);
}

TEST(SimClockTest, SameTimestampEventsPreserveFifoAcrossAdvanceCalls) {
  to::SimClock clock;
  std::vector<int> seen;
  clock.schedule_at(5, [&](to::SimClock::Time) { seen.push_back(1); });
  clock.schedule_at(5, [&](to::SimClock::Time) { seen.push_back(2); });
  clock.schedule_at(5, [&](to::SimClock::Time) { seen.push_back(3); });

  EXPECT_EQ(5, clock.advance_to_next());
  EXPECT_EQ(5, clock.advance_to_next());
  EXPECT_EQ(5, clock.advance_to_next());
  EXPECT_EQ((std::vector<int>{1, 2, 3}), seen);
}

TEST(SimClockTest, RunUntilAdvancesAcrossIdleGapAndKeepsFutureEventPending) {
  to::SimClock clock;
  clock.schedule_at(12, [](to::SimClock::Time) {});

  EXPECT_EQ(10, clock.run_until(10));
  EXPECT_EQ(10, clock.current_time());
  EXPECT_TRUE(clock.has_pending_events());
  EXPECT_EQ(12, clock.advance_to_next());
}

TEST(SimClockTest, SchedulingPastEventFromCallbackClampsToCurrentTime) {
  to::SimClock clock;
  std::vector<to::SimClock::Time> seen;
  clock.schedule_at(10, [&](to::SimClock::Time t) {
    seen.push_back(t);
    clock.schedule_at(5, [&](to::SimClock::Time inner_t) { seen.push_back(inner_t); });
  });

  EXPECT_EQ(10, clock.run_until(10));
  EXPECT_EQ((std::vector<to::SimClock::Time>{10, 10}), seen);
}

TEST(SimClockTest, ClearEventsDropsPendingWork) {
  to::SimClock clock;
  clock.schedule_at(1, [](to::SimClock::Time) {});
  clock.schedule_at(2, [](to::SimClock::Time) {});
  ASSERT_TRUE(clock.has_pending_events());

  clock.clear_events();
  EXPECT_FALSE(clock.has_pending_events());
}

TEST(SimClockTest, RunUntilDoesNotMoveBackwardInTime) {
  to::SimClock clock;
  clock.set_time(10);

  EXPECT_EQ(10, clock.run_until(5));
  EXPECT_EQ(10, clock.current_time());
}
}  // namespace
