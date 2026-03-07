#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__ACTOR_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__ACTOR_HPP_
#include <optional>
#include <vector>

#include "task_orchestrator/data/types.hpp"

namespace task_orchestrator {

struct Actor {
  ActorId id;
  /** Max concurrent tasks (or "slots") this actor can run. */
  int capacity = 1;
  /** Uptime windows; assignments must fall inside one of these. */
  std::vector<AvailabilityWindow> availability_windows;

  /** Current number of tasks assigned and not yet completed. */
  int current_load = 0;

  /** True if actor can take one more task at time t (capacity and availability). */
  bool can_accept_at(Time t, Duration duration) const;
  /** Next available start time >= t within availability. */
  std::optional<Time> next_available_start(Time t, Duration duration) const;
};

}  // namespace task_orchestrator

#endif
