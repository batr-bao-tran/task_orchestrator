#ifndef TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__TASK_EXECUTOR_HPP_
#define TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__TASK_EXECUTOR_HPP_

#include <concepts>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace task_orchestrator {

/** @brief Thin executor wrapper that owns a shared worker pool for background jobs. */
class TaskExecutor {
 public:
  /** @brief Construct an executor with the default worker count. */
  TaskExecutor();
  /** @brief Construct an executor with an explicit worker count. */
  explicit TaskExecutor(std::size_t worker_count);
  ~TaskExecutor() noexcept;

  TaskExecutor(const TaskExecutor&) = delete;
  TaskExecutor& operator=(const TaskExecutor&) = delete;
  TaskExecutor(TaskExecutor&&) = delete;
  TaskExecutor& operator=(TaskExecutor&&) = delete;

  template <typename Callable>
    requires std::invocable<std::decay_t<Callable>&>
  auto try_submit(Callable&& callable) -> std::future<std::invoke_result_t<std::decay_t<Callable>&>> {
    using Result = std::invoke_result_t<std::decay_t<Callable>&>;
    auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Callable>(callable));
    auto future = task->get_future();
    enqueue([task]() mutable { (*task)(); });
    return future;
  }

  /** @brief Return the number of worker threads owned by this executor. */
  [[nodiscard]] std::size_t worker_count() const noexcept;

 private:
  void enqueue(std::function<void()> task);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/** @brief Return the process-wide shared executor instance. */
TaskExecutor& get_shared_task_executor();

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__TASK_EXECUTOR_HPP_
