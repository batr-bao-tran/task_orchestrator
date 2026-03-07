#include "task_orchestrator/data/types.hpp"

#include <gtest/gtest.h>

namespace to = task_orchestrator;

TEST(TypesTest, AvailabilityWindowContains) {
  to::AvailabilityWindow w{100, 200};
  EXPECT_TRUE(w.contains(100));
  EXPECT_TRUE(w.contains(150));
  EXPECT_TRUE(w.contains(199));
  EXPECT_FALSE(w.contains(200));
  EXPECT_FALSE(w.contains(99));
}

TEST(TypesTest, AvailabilityWindowOverlaps) {
  to::AvailabilityWindow w{100, 200};
  EXPECT_TRUE(w.overlaps(50, 150));
  EXPECT_TRUE(w.overlaps(150, 250));
  EXPECT_TRUE(w.overlaps(100, 200));
  EXPECT_TRUE(w.overlaps(199, 201));
  EXPECT_FALSE(w.overlaps(50, 100));
  EXPECT_FALSE(w.overlaps(200, 250));
}
