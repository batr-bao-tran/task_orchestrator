#ifndef TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__GENERATOR_HPP_
#define TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__GENERATOR_HPP_
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace task_orchestrator {

/**
 * @brief Coroutine generator that yields T.
 *
 * @tparam T Type to yield.
 */
template <typename T>
class Generator {
 public:
  struct PromiseType {
    T current_{};
    std::exception_ptr exception_;

    Generator get_return_object() { return Generator{std::coroutine_handle<PromiseType>::from_promise(*this)}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) {
      current_ = std::move(value);
      return {};
    }
    void return_void() {}
    void unhandled_exception() { exception_ = std::current_exception(); }
  };
  using promise_type = PromiseType;

  struct Iterator {
    std::coroutine_handle<PromiseType> handle_;
    bool done_ = false;

    void advance() {
      if (done_) return;
      handle_.resume();
      if (handle_.done()) {
        done_ = true;
        if (handle_.promise().exception_) {
          std::rethrow_exception(handle_.promise().exception_);
        }
      }
    }

    Iterator& operator++() {
      advance();
      return *this;
    }
    bool operator!=(std::default_sentinel_t) const { return !done_; }
    const T& operator*() const { return handle_.promise().current_; }
  };

  explicit Generator(std::coroutine_handle<PromiseType> h) : handle_(h) {}
  ~Generator() noexcept {
    if (handle_) {
      handle_.destroy();
    }
  }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;
  Generator(Generator&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
  Generator& operator=(Generator&& other) noexcept {
    if (handle_) {
      handle_.destroy();
    }
    handle_ = std::exchange(other.handle_, nullptr);
    return *this;
  }

  Iterator begin() {
    Iterator it{handle_};
    it.advance();
    return it;
  }
  std::default_sentinel_t end() const { return {}; }

 private:
  std::coroutine_handle<PromiseType> handle_;
};

}  // namespace task_orchestrator

#endif
