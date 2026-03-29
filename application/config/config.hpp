#ifndef TASK_ORCHESTRATOR__APPLICATION_CONFIG__CONFIG_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_CONFIG__CONFIG_HPP_
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol/runtime_api.hpp"
#include "task_orchestrator/optimizer/backend.hpp"
#include "utils/types.hpp"

namespace task_orchestrator::app {

inline constexpr const char* const kDefaultOptimizationBackend = "auto";
inline constexpr const char* const kDefaultCliPrompt = "task-orchestrator> ";

struct AvailabilityWindowConfig {
  Time start = 0;
  Time end = 0;
};

struct ActorConfig {
  std::string id;
  std::string type;  // e.g. "robot", "machine"
  int capacity = 1;
  std::vector<AvailabilityWindowConfig> windows;
  std::vector<std::string> capabilities;
  double execution_cost_per_unit = 0.0;
};

struct TaskConfig {
  std::string id;
  Time requested_time = 0;  // release / available at
  Duration duration = 0;    // total or first phase
  Time latest_start_time = 0;
  Time deadline = 0;  // must complete by (0 = no hard deadline)
  Priority priority = 0;
  int demand = 1;
  bool mandatory = true;
  bool preemptible = false;
  std::vector<std::string> allowed_actor_types;
  std::vector<std::string> allowed_actor_ids;
  std::vector<std::string> preferred_actor_ids;
  std::vector<std::string> required_capabilities;
  std::vector<std::string> dependency_task_ids;
  std::vector<std::string> mutually_exclusive_task_ids;
  std::unordered_map<std::string, Time> actor_distances;
  double tardiness_cost_per_unit = 0.0;
  double early_start_bonus = 0.0;
  /** Optional: per-phase durations (sub-processes). */
  std::vector<Duration> phase_durations;
};

struct WorkflowConfig {
  struct OptimizationConfig {
    struct ObjectiveConfig {
      int64_t fulfilled_task_weight = optimizer::kDefaultFulfilledTaskWeight;
      int64_t priority_weight = optimizer::kDefaultPriorityWeight;
      int64_t makespan_weight = optimizer::kDefaultMakespanWeight;
      int64_t travel_distance_weight = optimizer::kDefaultTravelDistanceWeight;
      int64_t tardiness_weight = optimizer::kDefaultTardinessWeight;
      int64_t execution_cost_weight = optimizer::kDefaultExecutionCostWeight;
      int64_t preferred_actor_weight = optimizer::kDefaultPreferredActorWeight;
    };

    std::string backend = kDefaultOptimizationBackend;
    int64_t time_limit_ms = 0;
    double relative_gap_limit = 0.0;
    int num_search_workers = 1;
    bool allow_partial_plan = true;
    ObjectiveConfig objective;
  };

  std::string id;
  OptimizationConfig optimization;
  std::vector<ActorConfig> actors;
  std::vector<TaskConfig> tasks;
};

/** @brief CLI launcher settings for the long-running application binary. */
struct CliInterfaceConfig {
  bool enabled = false;
  std::string prompt = kDefaultCliPrompt;
};

/** @brief HTTP server launcher settings for the long-running application binary. */
struct HttpInterfaceConfig {
  bool enabled = false;
  protocol::HttpEndpointOptions endpoint;
};

/** @brief gRPC server launcher settings for the long-running application binary. */
struct GrpcInterfaceConfig {
  bool enabled = false;
  protocol::GrpcEndpointOptions endpoint;
};

/** @brief Configurable interfaces exposed by the application binary. */
struct ApplicationInterfaceConfig {
  CliInterfaceConfig cli;
  HttpInterfaceConfig http;
  GrpcInterfaceConfig grpc;
};

/** @brief Durable control-plane settings layered above the runtime API. */
struct ControlPlaneConfig {
  bool enabled = false;
  std::string database_path;
  bool recover_on_start = true;
  std::int32_t prune_after_days = 30;

  [[nodiscard]] bool configured() const noexcept { return enabled && !database_path.empty(); }
  [[nodiscard]] bool pruning_enabled() const noexcept { return prune_after_days > 0; }
};

/** @brief Supported input file kinds for the application launcher. */
enum class RequestFileKind {
  None,
  WorkflowYaml,
  WorkflowText,
};

/** @brief File-backed workflow request consumed by one-shot or bootstrap execution. */
struct RequestFileConfig {
  RequestFileKind kind = RequestFileKind::None;
  std::string path;

  [[nodiscard]] bool configured() const noexcept { return kind != RequestFileKind::None && !path.empty(); }
};

/** @brief Launcher configuration for the long-running application binary. */
struct ApplicationLaunchConfig {
  bool configured = false;
  protocol::SecurityConfig security;
  ApplicationInterfaceConfig interfaces;
  ControlPlaneConfig control_plane;
  RequestFileConfig bootstrap_request;
  std::optional<WorkflowConfig> bootstrap_workflow;

  [[nodiscard]] bool has_enabled_interface() const noexcept {
    return interfaces.cli.enabled || interfaces.http.enabled || interfaces.grpc.enabled;
  }
};

/** @brief High-level application mode selected by the launcher config. */
enum class ApplicationMode {
  OneShot,
  Serve,
};

/** @brief Top-level executable configuration loaded by the application launcher. */
struct ApplicationConfig {
  bool configured = false;
  ApplicationMode mode = ApplicationMode::OneShot;
  RequestFileConfig request;
  ApplicationLaunchConfig service;

  [[nodiscard]] bool valid() const noexcept {
    return mode == ApplicationMode::OneShot ? request.configured() : service.has_enabled_interface();
  }
};

/** @brief Load a workflow definition from YAML. Returns empty on parse error. */
WorkflowConfig load_config_from_file(const char* path);
WorkflowConfig load_config_from_string(const std::string& content);
/** @brief Load application launch settings for the long-running runtime service. */
ApplicationLaunchConfig load_launch_config_from_file(const char* path);
/** @brief Load application launch settings from YAML text. */
ApplicationLaunchConfig load_launch_config_from_string(const std::string& content);
/** @brief Load top-level executable configuration from YAML. */
ApplicationConfig load_application_config_from_file(const char* path);
/** @brief Load top-level executable configuration from YAML text. */
ApplicationConfig load_application_config_from_string(const std::string& content);

}  // namespace task_orchestrator::app

#endif  // TASK_ORCHESTRATOR__APPLICATION_CONFIG__CONFIG_HPP_
