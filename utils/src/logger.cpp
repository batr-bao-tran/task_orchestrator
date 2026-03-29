#include "utils/logger.hpp"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <mutex>

namespace task_orchestrator {
namespace {

inline constexpr const char* kLogPattern = "%Y-%m-%d %H:%M:%S.%e [%n] [%^%l%$] %v";

inline constexpr std::array<const char*, 4> kLayerNames = {
    "utils",
    "application",
    "core",
    "optimizer",
};

constexpr std::size_t to_index(LogLayer layer) { return static_cast<std::size_t>(layer); }

std::shared_ptr<spdlog::logger> create_logger(LogLayer layer) {
  const char* logger_name = kLayerNames[to_index(layer)];
  auto logger = spdlog::stdout_color_mt(logger_name);
  logger->set_pattern(kLogPattern);
  logger->set_level(spdlog::level::info);
  logger->flush_on(spdlog::level::warn);
  return logger;
}

}  // namespace

std::shared_ptr<spdlog::logger> get_logger(LogLayer layer) {
  static std::array<std::shared_ptr<spdlog::logger>, kLayerNames.size()> loggers;
  static std::array<std::once_flag, kLayerNames.size()> once_flags;

  const std::size_t index = to_index(layer);
  std::call_once(once_flags[index], [layer, index]() { loggers[index] = create_logger(layer); });
  return loggers[index];
}

}  // namespace task_orchestrator
