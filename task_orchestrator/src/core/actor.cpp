#include "task_orchestrator/core/actor.hpp"

#include <algorithm>
#include <optional>

#include "task_orchestrator/data/types.hpp"

namespace task_orchestrator {

bool Actor::can_accept_at(Time t, Duration duration) const {
  if (current_load >= capacity) {
    return false;
  }
  Time end = t + duration;
  for (const auto& w : availability_windows) {
    if (w.contains(t) && end <= w.end) {
      return true;
    }
  }
  return false;
}

std::optional<Time> Actor::next_available_start(Time t, Duration duration) const {
  if (current_load >= capacity) {
    return std::nullopt;
  }
  Time candidate = t;
  std::vector<AvailabilityWindow> sorted(availability_windows.begin(), availability_windows.end());
  std::ranges::sort(sorted, {}, &AvailabilityWindow::start);
  for (const auto& window : sorted) {
    if (candidate < window.start) {
      candidate = window.start;
    }
    if (candidate + duration <= window.end) {
      return candidate;
    }
    candidate = window.end;
  }
  return std::nullopt;
}

}  // namespace task_orchestrator
