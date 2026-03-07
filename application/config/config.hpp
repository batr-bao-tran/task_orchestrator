#ifndef TASK_ORCHESTRATOR__APPLICATION_CONFIG__CONFIG_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_CONFIG__CONFIG_HPP_
#include <string>
#include <vector>

#include "utils/types.hpp"

namespace task_orchestrator::app {

struct AvailabilityWindowConfig {
  Time start = 0;
  Time end = 0;
};

struct ActorConfig {
  std::string id;
  std::string type;  // e.g. "robot", "machine"
  int capacity = 1;
  std::vector<AvailabilityWindowConfig> windows;
};

struct TaskConfig {
  std::string id;
  Time requested_time = 0;  // release / available at
  Duration duration = 0;    // total or first phase
  Time deadline = 0;        // must complete by (0 = no hard deadline)
  std::vector<std::string> allowed_actor_types;
  /** Optional: per-phase durations (sub-processes). */
  std::vector<Duration> phase_durations;
};

struct WorkflowConfig {
  std::string id;
  std::vector<ActorConfig> actors;
  std::vector<TaskConfig> tasks;
};

/** Load from YAML config (see README). Returns empty on parse error. */
WorkflowConfig load_config_from_file(const char* path);
WorkflowConfig load_config_from_string(const std::string& content);

}  // namespace task_orchestrator::app

#endif
