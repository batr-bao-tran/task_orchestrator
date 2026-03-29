#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUNNER_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUNNER_HPP_
#include <string_view>

#include "config/config.hpp"
#include "runner/run_result.hpp"

namespace task_orchestrator::app {

using OptimizationRequest = WorkflowConfig;

/**
 * @brief Execute workflow scheduling directly from a structured config.
 */
RunResult run(const WorkflowConfig& config);
/**
 * @brief Optimize a structured workflow request with the configured backend.
 */
RunResult optimize(const OptimizationRequest& config);
/**
 * @brief Parse controlled natural-language workflow text and optimize it.
 */
RunResult optimize_text(std::string_view request);

}  // namespace task_orchestrator::app

#endif  // TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__RUNNER_HPP_
