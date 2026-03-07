#include <cstring>
#include <iostream>

#include "config/config.hpp"
#include "runner/runner.hpp"

int main(int argc, char** argv) {
  const char* path = argc >= 2 ? argv[1] : nullptr;
  if (!path || std::strcmp(path, "-") == 0) {
    std::cerr << "Usage: " << (argc ? argv[0] : "run_config")
              << " <config_file>\n"
                 "  config_file: path to YAML config (or '-' for stdin).\n"
                 "  See application/examples/*.yaml for format.\n";
    return path ? 0 : 1;
  }

  task_orchestrator::app::WorkflowConfig config;
  if (std::strcmp(path, "-") == 0) {
    const std::string content{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    config = task_orchestrator::app::load_config_from_string(content);
  } else {
    config = task_orchestrator::app::load_config_from_file(path);
  }

  if (config.tasks.empty() && config.actors.empty()) {
    std::cerr << "Empty or invalid config.\n";
    return 1;
  }

  task_orchestrator::app::RunResult result = task_orchestrator::app::run(config);

  if (!result.ok) {
    std::cerr << "Run failed: " << result.error_message << "\n";
    return 1;
  }

  std::cout << "Assignments: " << result.assignments.size() << "\n";
  for (const auto& a : result.assignments) {
    std::cout << "  " << a.task_id << " -> " << a.actor_id << " @ " << a.start_time << "\n";
  }

  if (result.capacity_issue) {
    std::cout << "Capacity issue: " << result.unfulfilled_task_ids.size() << " task(s) could not be fulfilled.\n";
    for (const auto& tid : result.unfulfilled_task_ids) {
      std::cout << "  unfulfilled: " << tid << "\n";
    }
  }

  return result.capacity_issue ? 2 : 0;  // 2 = partial success
}
