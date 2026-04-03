#include "utils/task_executor.hpp"

#include <algorithm>
#include <boost/asio/thread_pool.hpp>
#include <cstddef>
#include <memory>
#include <thread>

namespace task_orchestrator {
namespace {

inline constexpr std::size_t kMinimumSharedExecutorWorkerCount = 2;
inline constexpr std::size_t kMinimumExecutorWorkerCount = 1;

std::size_t default_shared_executor_worker_count() {
  return std::max(kMinimumSharedExecutorWorkerCount, static_cast<std::size_t>(std::thread::hardware_concurrency()));
}

}  // namespace

struct TaskExecutor::Impl {
  boost::asio::thread_pool thread_pool;
  std::size_t configured_worker_count = 0;

  explicit Impl(const std::size_t worker_count) : thread_pool(worker_count), configured_worker_count(worker_count) {}
};

TaskExecutor::TaskExecutor() : TaskExecutor(default_shared_executor_worker_count()) {}

TaskExecutor::TaskExecutor(const std::size_t worker_count) {
  const std::size_t effective_worker_count = std::max(kMinimumExecutorWorkerCount, worker_count);
  impl_ = std::make_unique<Impl>(effective_worker_count);
}

TaskExecutor::~TaskExecutor() noexcept { stop(); }

void TaskExecutor::stop() noexcept {
  impl_->thread_pool.stop();
  impl_->thread_pool.join();
}

std::size_t TaskExecutor::worker_count() const noexcept { return impl_->configured_worker_count; }

void TaskExecutor::enqueue(std::function<void()> task) {
  impl_->thread_pool.get_executor().post(std::move(task), std::allocator<void>{});
}

TaskExecutor& get_shared_task_executor() {
  static TaskExecutor executor;
  return executor;
}

}  // namespace task_orchestrator
