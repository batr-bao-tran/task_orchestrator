#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_UTILS_INCLUDE_TASK_ORCHESTRATOR_UTILS__SIM_CLOCK_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_UTILS_INCLUDE_TASK_ORCHESTRATOR_UTILS__SIM_CLOCK_HPP_
#include <functional>
#include <queue>
#include <vector>

#include "task_orchestrator/data/types.hpp"

namespace task_orchestrator {

/** Discrete event simulation clock and event queue. */
class SimClock {
 public:
  using Time = task_orchestrator::Time;
  using EventCallback = std::function<void(Time)>;

  SimClock() : now_(0) {}

  Time current_time() const { return now_; }
  void set_time(Time t) { now_ = t; }
  bool has_pending_events() const { return !queue_.empty(); }

  /** Schedule a one-off event at time t. */
  void schedule_at(Time t, EventCallback cb);

  /** Advance to the next event time and run all events at that time. Returns new time. */
  Time advance_to_next();

  /** Run until time limit or no events. Returns final time. */
  Time run_until(Time time_limit);

  void clear_events() {
    while (!queue_.empty()) {
      queue_.pop();
    }
  }

 private:
  Time now_;
  struct Event {
    Time at;
    EventCallback cb;
    bool operator>(const Event& o) const { return at > o.at; }
  };
  std::priority_queue<Event, std::vector<Event>, std::greater<Event>> queue_;
};

}  // namespace task_orchestrator

#endif
