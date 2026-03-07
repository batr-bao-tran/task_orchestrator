#include "task_orchestrator/core/actor.hpp"

#include <algorithm>
#include <optional>

#include "utils/types.hpp"

namespace task_orchestrator {

bool Actor::can_accept_at(Time t, Duration duration) const {
  if (current_load >= capacity) {
    return false;
  }
  Time end = t + duration;
  return std::ranges::any_of(availability_windows, [t, end](const auto& w) { return w.contains(t) && end <= w.end; });
}

std::optional<Time> Actor::next_available_start(Time t, Duration duration) const {
  if (current_load >= capacity) {
    return std::nullopt;
  }
  Time candidate = t;
  std::vector<AvailabilityWindow> sorted(availability_windows.begin(), availability_windows.end());
  std::ranges::sort(sorted, {}, &AvailabilityWindow::start);
  for (const auto& window : sorted) {
    candidate = std::max(candidate, window.start);
    if (candidate + duration <= window.end) {
      return candidate;
    }
    candidate = window.end;
  }
  return std::nullopt;
}

}  // namespace task_orchestrator
