#include "utils/task_executor.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <numeric>
#include <vector>

namespace {
namespace to = task_orchestrator;

TEST(TaskExecutorTest, SubmitReturnsFutureValue) {
  to::TaskExecutor executor(2);

  auto future = executor.try_submit([]() { return 42; });
  EXPECT_EQ(future.get(), 42);
}

TEST(TaskExecutorTest, ExecutesAllSubmittedTasks) {
  constexpr int kTaskCount = 16;

  to::TaskExecutor executor(4);
  std::atomic<int> completed_task_count = 0;
  std::vector<std::future<int>> futures;
  futures.reserve(kTaskCount);

  for (int task_index = 0; task_index < kTaskCount; ++task_index) {
    auto future = executor.try_submit([task_index, &completed_task_count]() {
      completed_task_count.fetch_add(1, std::memory_order_relaxed);
      return task_index * task_index;
    });
    futures.push_back(std::move(future));
  }

  std::vector<int> results;
  results.reserve(futures.size());
  for (auto& future : futures) {
    results.push_back(future.get());
  }

  EXPECT_EQ(completed_task_count.load(std::memory_order_relaxed), kTaskCount);
  EXPECT_EQ(std::accumulate(results.begin(), results.end(), 0), 1240);
}

TEST(TaskExecutorTest, SharedExecutorIsSingleton) {
  EXPECT_EQ(&to::get_shared_task_executor(), &to::get_shared_task_executor());
}

TEST(TaskExecutorTest, ReportsConfiguredWorkerCount) {
  constexpr std::size_t kWorkerCount = 3;
  to::TaskExecutor executor(kWorkerCount);

  EXPECT_EQ(executor.worker_count(), kWorkerCount);
}

TEST(TaskExecutorTest, ZeroWorkerCountClampsToMinimumWorker) {
  to::TaskExecutor executor(0);

  EXPECT_EQ(executor.worker_count(), 1U);
}

}  // namespace
