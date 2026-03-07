#include "task_orchestrator/utils/sim_clock.hpp"

namespace task_orchestrator {

void SimClock::schedule_at(Time t, EventCallback cb) { queue_.push(Event{t, std::move(cb)}); }

SimClock::Time SimClock::advance_to_next() {
  if (queue_.empty()) return now_;
  Event e = queue_.top();
  queue_.pop();
  now_ = e.at;
  if (e.cb) {
    e.cb(now_);
  }
  return now_;
}

SimClock::Time SimClock::run_until(Time time_limit) {
  while (!queue_.empty() && queue_.top().at <= time_limit) {
    advance_to_next();
  }
  if (now_ > time_limit) {
    now_ = time_limit;
  }
  return now_;
}

}  // namespace task_orchestrator
