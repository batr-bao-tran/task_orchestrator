#include "task_orchestrator/data/process.hpp"

#include <gtest/gtest.h>

namespace to = task_orchestrator;

TEST(ProcessTest, TaskIdsNoSubprocesses) {
  to::Process p{"P1", "ph", {}, 5, 0, {}};
  auto tids = p.task_ids();
  ASSERT_EQ(1u, tids.size());
  EXPECT_EQ("P1", tids[0]);
}

TEST(ProcessTest, TaskIdsWithSubprocesses) {
  to::Process p{"P1", "ph", {"SP1", "SP2"}, 5, 0, {}};
  auto tids = p.task_ids();
  ASSERT_EQ(3u, tids.size());
  EXPECT_EQ("P1", tids[0]);
  EXPECT_EQ("SP1", tids[1]);
  EXPECT_EQ("SP2", tids[2]);
}
