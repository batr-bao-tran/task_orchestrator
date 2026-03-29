#include "utils/logger.hpp"

#include <gtest/gtest.h>

namespace {
namespace to = task_orchestrator;

TEST(LoggerTest, ReturnsStableLoggerPerLayerAndDistinctNames) {
  const auto utils_logger = to::get_logger(to::LogLayer::Utils);
  const auto application_logger = to::get_logger(to::LogLayer::Application);
  const auto core_logger = to::get_logger(to::LogLayer::Core);
  const auto optimizer_logger = to::get_logger(to::LogLayer::Optimizer);

  EXPECT_EQ(utils_logger, to::get_logger(to::LogLayer::Utils));
  EXPECT_EQ(application_logger, to::get_logger(to::LogLayer::Application));
  EXPECT_EQ("utils", utils_logger->name());
  EXPECT_EQ("application", application_logger->name());
  EXPECT_EQ("core", core_logger->name());
  EXPECT_EQ("optimizer", optimizer_logger->name());
  EXPECT_NE(utils_logger, application_logger);
  EXPECT_NE(core_logger, optimizer_logger);
}

}  // namespace
