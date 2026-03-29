#ifndef TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__LOGGER_HPP_
#define TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__LOGGER_HPP_
#include <spdlog/logger.h>

#include <memory>

namespace task_orchestrator {

enum class LogLayer {
  Utils,
  Application,
  Core,
  Optimizer,
};

/** @brief Return the singleton logger associated with the requested layer. */
std::shared_ptr<spdlog::logger> get_logger(LogLayer layer);

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__UTILS_INCLUDE_UTILS__LOGGER_HPP_
