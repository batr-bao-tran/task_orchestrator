#include "task_orchestrator/data/process.hpp"

#include <gtest/gtest.h>

namespace {
namespace to = task_orchestrator;

TEST(ProcessTest, TaskIdsNoSubprocesses) {
  to::Process p{
      .id = "P1", .phase_id = "ph", .sub_process_ids = {}, .estimated_duration = 5, .priority = 0, .deadline = {}};
  auto tids = p.task_ids();
  ASSERT_EQ(1U, tids.size());
  EXPECT_EQ("P1", tids[0]);
}

TEST(ProcessTest, TaskIdsWithSubprocesses) {
  to::Process p{.id = "P1",
                .phase_id = "ph",
                .sub_process_ids = {"SP1", "SP2"},
                .estimated_duration = 5,
                .priority = 0,
                .deadline = {}};
  auto tids = p.task_ids();
  ASSERT_EQ(3U, tids.size());
  EXPECT_EQ("P1", tids[0]);
  EXPECT_EQ("SP1", tids[1]);
  EXPECT_EQ("SP2", tids[2]);
}
}  // namespace
