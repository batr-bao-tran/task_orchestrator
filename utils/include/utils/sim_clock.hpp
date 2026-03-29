#ifndef TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__SIM_CLOCK_HPP_
#define TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__SIM_CLOCK_HPP_
#include <functional>
#include <queue>
#include <vector>

#include "utils/types.hpp"

namespace task_orchestrator {

/** Discrete event simulation clock and event queue.
 *  Events at the same time are executed in FIFO (insertion) order.
 *  Events scheduled in the past are clamped to the current simulated time so the clock stays monotonic. */
class SimClock {
 public:
  using Time = task_orchestrator::Time;
  using EventCallback = std::function<void(Time)>;

  SimClock() = default;

  Time current_time() const { return now_; }
  void set_time(Time t) { now_ = t; }
  bool has_pending_events() const { return !queue_.empty(); }

  /** Schedule a one-off event at time t. */
  void schedule_at(Time t, EventCallback cb);

  /** Advance to the next queued event and run exactly one callback. Returns the new simulated time. */
  Time advance_to_next();

  /** Run all events up to \p time_limit and advance through idle gaps. Returns the resulting simulated time. */
  Time run_until(Time time_limit);

  void clear_events() {
    while (!queue_.empty()) {
      queue_.pop();
    }
    next_seq_ = 0;
  }

 private:
  Time now_{0};
  uint64_t next_seq_{0};
  struct Event {
    Time at;
    uint64_t seq;
    EventCallback cb;
    bool operator>(const Event& o) const { return (at != o.at) ? (at > o.at) : (seq > o.seq); }
  };
  std::priority_queue<Event, std::vector<Event>, std::greater<>> queue_;
};

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__SIM_CLOCK_HPP_
