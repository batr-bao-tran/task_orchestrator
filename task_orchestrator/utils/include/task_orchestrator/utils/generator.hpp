#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_UTILS_INCLUDE_TASK_ORCHESTRATOR_UTILS__GENERATOR_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_UTILS_INCLUDE_TASK_ORCHESTRATOR_UTILS__GENERATOR_HPP_
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace task_orchestrator {

/** C++20 coroutine generator that yields T. */
template <typename T>
class Generator {
 public:
  struct promise_type {
    T current_{};
    std::exception_ptr exception_;

    Generator get_return_object() { return Generator{std::coroutine_handle<promise_type>::from_promise(*this)}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) {
      current_ = std::move(value);
      return {};
    }
    void return_void() {}
    void unhandled_exception() { exception_ = std::current_exception(); }
  };

  struct iterator {
    std::coroutine_handle<promise_type> h_;
    bool done_ = false;

    void advance() {
      if (done_) return;
      h_.resume();
      if (h_.done()) {
        done_ = true;
        if (h_.promise().exception_) {
          std::rethrow_exception(h_.promise().exception_);
        }
      }
    }

    iterator& operator++() {
      advance();
      return *this;
    }
    bool operator!=(std::default_sentinel_t) const { return !done_; }
    const T& operator*() const { return h_.promise().current_; }
  };

  explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
  ~Generator() {
    if (h_) {
      h_.destroy();
    }
  }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;
  Generator(Generator&& other) noexcept : h_(std::exchange(other.h_, nullptr)) {}
  Generator& operator=(Generator&& other) noexcept {
    if (h_) {
      h_.destroy();
    }
    h_ = std::exchange(other.h_, nullptr);
    return *this;
  }

  iterator begin() {
    iterator it{h_};
    it.advance();
    return it;
  }
  std::default_sentinel_t end() const { return {}; }

 private:
  std::coroutine_handle<promise_type> h_;
};

}  // namespace task_orchestrator

#endif
