#include "task_orchestrator/core/actor.hpp"

#include <gtest/gtest.h>

namespace to = task_orchestrator;

TEST(ActorTest, CanAcceptAt) {
  to::Actor a;
  a.id = "A1";
  a.capacity = 2;
  a.current_load = 0;
  a.availability_windows = {{0, 1000}};
  EXPECT_TRUE(a.can_accept_at(0, 10));
  EXPECT_TRUE(a.can_accept_at(990, 10));
  EXPECT_FALSE(a.can_accept_at(991, 10));

  a.capacity = 1;
  a.current_load = 1;
  EXPECT_FALSE(a.can_accept_at(0, 10));
}

TEST(ActorTest, NextAvailableStart) {
  to::Actor a;
  a.current_load = 0;
  a.availability_windows = {{100, 200}, {300, 400}};

  auto t = a.next_available_start(0, 50);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(100, *t);
  t = a.next_available_start(150, 50);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(150, *t);
  t = a.next_available_start(250, 50);
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(300, *t);
}

TEST(ActorTest, NextAvailableStartFullCapacity) {
  to::Actor a;
  a.capacity = 1;
  a.current_load = 1;
  a.availability_windows = {{0, 1000}};
  EXPECT_FALSE(a.next_available_start(0, 10).has_value());
}
