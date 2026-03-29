#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__APPLICATION_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__APPLICATION_HPP_

#include <filesystem>

#include "config/config.hpp"

namespace task_orchestrator::app {

/**
 * @brief Executable launcher that runs one-shot requests or long-lived runtime services.
 */
class Application final {
 public:
  Application() = default;
  ~Application() noexcept = default;

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  /**
   * @brief Parse launcher arguments and execute the requested application config.
   */
  static int run_from_args(int argc, char** argv);

  /**
   * @brief Load and execute a launcher config from a file or from stdin when path is "-".
   */
  static int run_from_file(const char* config_path);

  /**
   * @brief Execute an already parsed application config using the given base directory for relative paths.
   */
  static int run(const ApplicationConfig& config, const std::filesystem::path& config_directory = {});
};

}  // namespace task_orchestrator::app

#endif  // TASK_ORCHESTRATOR__APPLICATION_RUNNER_INCLUDE_RUNNER__APPLICATION_HPP_
