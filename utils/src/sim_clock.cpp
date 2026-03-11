#include "utils/sim_clock.hpp"

#include <algorithm>

namespace task_orchestrator {

void SimClock::schedule_at(Time t, EventCallback cb) {
  queue_.push(Event{.at = t, .seq = next_seq_++, .cb = std::move(cb)});
}

SimClock::Time SimClock::advance_to_next() {
  if (queue_.empty()) return now_;
  Event event = queue_.top();
  queue_.pop();
  now_ = event.at;
  if (event.cb) {
    event.cb(now_);
  }
  return now_;
}

SimClock::Time SimClock::run_until(Time time_limit) {
  while (!queue_.empty() && queue_.top().at <= time_limit) {
    advance_to_next();
  }
  now_ = std::min(now_, time_limit);
  return now_;
}

}  // namespace task_orchestrator
