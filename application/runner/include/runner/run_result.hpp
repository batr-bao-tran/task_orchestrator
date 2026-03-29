#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUN_RESULT_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUN_RESULT_HPP_
#include <vector>

#include "task_orchestrator/core/scheduler.hpp"

namespace task_orchestrator::app {

/** @brief Final scheduling outcome returned by the runner and optimizer APIs. */
struct RunResult {
  bool ok = true;
  /** True if at least one task could not be fulfilled (capacity/availability). */
  bool capacity_issue = false;
  std::vector<task_orchestrator::Assignment> assignments;
  /** Task IDs that could not be assigned (when optimizing for max productivity). */
  std::vector<task_orchestrator::TaskId> unfulfilled_task_ids;
  std::string error_message;
};

}  // namespace task_orchestrator::app

#endif  // TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUN_RESULT_HPP_
