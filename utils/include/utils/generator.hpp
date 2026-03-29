#ifndef TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__GENERATOR_HPP_
#define TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__GENERATOR_HPP_
#include <coroutine>
#include <exception>
#include <string>
#include <string_view>
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
    bool failed_ = false;
    std::string error_message_;

    Generator get_return_object() { return Generator{std::coroutine_handle<PromiseType>::from_promise(*this)}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) {
      current_ = std::move(value);
      return {};
    }
    void return_void() {}
    void unhandled_exception() noexcept {
      failed_ = true;
      error_message_ = "Generator coroutine terminated with an unknown exception.";
      try {
        std::rethrow_exception(std::current_exception());
      } catch (const std::exception& exception) {
        error_message_ = exception.what();
      } catch (const char* message) {
        error_message_ = message != nullptr ? message : "Generator coroutine terminated with a null string exception.";
      } catch (...) {
        error_message_ = "Generator coroutine terminated with a non-standard exception.";
      }
    }
  };
  using promise_type = PromiseType;

  struct Sentinel {};

  struct Iterator {
    std::coroutine_handle<PromiseType> handle_;
    bool done_ = false;

    void advance() noexcept {
      if (done_) return;
      handle_.resume();
      done_ = handle_.done();
    }

    Iterator& operator++() {
      advance();
      return *this;
    }
    bool operator!=(Sentinel) const noexcept { return !done_; }
    const T& operator*() const { return handle_.promise().current_; }
  };

  explicit Generator(std::coroutine_handle<PromiseType> h) noexcept : handle_(h) {}
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

  Iterator begin() noexcept {
    Iterator it{handle_};
    it.advance();
    return it;
  }
  Sentinel end() const noexcept { return {}; }
  [[nodiscard]] bool failed() const noexcept { return handle_ && handle_.promise().failed_; }
  [[nodiscard]] std::string_view error_message() const noexcept {
    return handle_ ? std::string_view(handle_.promise().error_message_) : std::string_view{};
  }

 private:
  std::coroutine_handle<PromiseType> handle_;
};

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__GENERATOR_HPP_
