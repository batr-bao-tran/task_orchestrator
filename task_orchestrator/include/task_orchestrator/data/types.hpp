#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__TYPES_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__TYPES_HPP_
#include <cstdint>
#include <string>
#include <vector>

namespace task_orchestrator {

using WorkflowId = std::string;
using PhaseId = std::string;
using ProcessId = std::string;
using SubProcessId = std::string;
using TaskId = std::string;  // Process or SubProcess identifier for scheduling
using ActorId = std::string;

/** Wall-clock or logical time (e.g. seconds since epoch or tick index). */
using Time = int64_t;

/** Duration in same units as Time. */
using Duration = int64_t;

/** Priority: higher = more important (scheduler prefers higher first). */
using Priority = int32_t;

struct AvailabilityWindow {
  Time start = 0;
  Time end = 0;

  bool contains(Time t) const { return t >= start && t < end; }
  bool overlaps(Time range_start, Time range_end) const { return range_start < end && range_end > start; }
};

}  // namespace task_orchestrator

#endif
