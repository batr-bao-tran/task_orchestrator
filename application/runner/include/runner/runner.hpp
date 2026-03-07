#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUNNER_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUNNER_HPP_
#include "config/config.hpp"
#include "runner/run_result.hpp"

namespace task_orchestrator::app {

/**
 * Builds workflow and actors from config, runs EDF-based scheduler to maximize
 * fulfilled tasks. Sets capacity_issue and unfulfilled_task_ids when not all
 * tasks can be fulfilled.
 */
RunResult run(const WorkflowConfig& config);

}  // namespace task_orchestrator::app

#endif
