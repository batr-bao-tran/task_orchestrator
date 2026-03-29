#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "config/config.hpp"
#include "utils/logger.hpp"

namespace task_orchestrator::app {

namespace {

constexpr const char* const kDefaultWorkflowId = "workflow";
constexpr const char* const kApplicationSectionName = "application";
constexpr const char* const kLaunchSectionName = "launch";
constexpr const char* const kWorkflowSectionName = "workflow";
constexpr const char* const kServiceSectionName = "service";
constexpr const char* const kControlPlaneSectionName = "control_plane";
constexpr Time kDefaultAvailabilityWindowStart = 0;
constexpr Time kDefaultAvailabilityWindowEnd = 1000000;
constexpr Time kDefaultDeadlineSlack = 1000;

template <typename OutputValue, typename TransformFn>
void append_transformed_sequence(const YAML::Node& sequence_node,
                                 std::vector<OutputValue>& output_values,
                                 TransformFn&& transform_entry) {
  if (!sequence_node || !sequence_node.IsSequence()) {
    return;
  }

  output_values.reserve(output_values.size() + sequence_node.size());
  for (const YAML::Node& entry_node : sequence_node) {
    output_values.push_back(transform_entry(entry_node));
  }
}

void append_actor_distance_overrides(const YAML::Node& actor_distance_map,
                                     std::unordered_map<std::string, Time>& actor_distances) {
  if (!actor_distance_map || !actor_distance_map.IsMap()) {
    return;
  }

  actor_distances.reserve(actor_distances.size() + actor_distance_map.size());
  for (const auto& actor_distance_entry : actor_distance_map) {
    actor_distances.emplace(actor_distance_entry.first.as<std::string>(),
                            static_cast<Time>(actor_distance_entry.second.as<int64_t>()));
  }
}

AvailabilityWindowConfig parse_availability_window(const YAML::Node& window_node) {
  AvailabilityWindowConfig availability_window;
  if (window_node["start"]) {
    availability_window.start = static_cast<Time>(window_node["start"].as<int64_t>());
  }
  if (window_node["end"]) {
    availability_window.end = static_cast<Time>(window_node["end"].as<int64_t>());
  }
  return availability_window;
}

protocol::TlsDataSource parse_tls_data_source(const YAML::Node& source_node) {
  if (!source_node) {
    return {};
  }
  if (source_node.IsScalar()) {
    return protocol::TlsDataSource::from_file(source_node.as<std::string>());
  }
  if (!source_node.IsMap()) {
    return {};
  }
  if (source_node["file"]) {
    return protocol::TlsDataSource::from_file(source_node["file"].as<std::string>());
  }
  if (source_node["inline_pem"]) {
    return protocol::TlsDataSource::from_inline_pem(source_node["inline_pem"].as<std::string>());
  }
  return {};
}

protocol::TlsIdentityConfig parse_tls_identity_config(const YAML::Node& identity_node) {
  protocol::TlsIdentityConfig identity_config;
  if (!identity_node || !identity_node.IsMap()) {
    return identity_config;
  }

  identity_config.certificate_chain = parse_tls_data_source(identity_node["certificate_chain"]);
  identity_config.private_key = parse_tls_data_source(identity_node["private_key"]);
  identity_config.private_key_password = parse_tls_data_source(identity_node["private_key_password"]);
  return identity_config;
}

protocol::TlsTrustConfig parse_tls_trust_config(const YAML::Node& trust_node) {
  protocol::TlsTrustConfig trust_config;
  if (!trust_node || !trust_node.IsMap()) {
    return trust_config;
  }

  trust_config.root_certificates = parse_tls_data_source(trust_node["root_certificates"]);
  if (trust_node["use_system_default_roots"]) {
    trust_config.use_system_default_roots = trust_node["use_system_default_roots"].as<bool>();
  }
  if (trust_node["verify_peer"]) {
    trust_config.verify_peer = trust_node["verify_peer"].as<bool>();
  }
  if (trust_node["expected_peer_name"]) {
    trust_config.expected_peer_name = trust_node["expected_peer_name"].as<std::string>();
  }
  return trust_config;
}

protocol::TlsServerConfig parse_tls_server_config(const YAML::Node& tls_node) {
  protocol::TlsServerConfig tls_config;
  if (!tls_node || !tls_node.IsMap()) {
    return tls_config;
  }

  tls_config.identity = parse_tls_identity_config(tls_node["identity"]);
  tls_config.client_trust = parse_tls_trust_config(tls_node["client_trust"]);
  if (tls_node["require_client_certificate"]) {
    tls_config.require_client_certificate = tls_node["require_client_certificate"].as<bool>();
  }
  return tls_config;
}

protocol::AuthMode parse_auth_mode(const YAML::Node& security_node) {
  if (!security_node || !security_node.IsMap() || !security_node["mode"]) {
    return protocol::AuthMode::None;
  }

  const auto mode = security_node["mode"].as<std::string>();
  if (mode == "bearer_token") {
    return protocol::AuthMode::BearerToken;
  }
  if (mode == "api_key") {
    return protocol::AuthMode::ApiKey;
  }
  if (mode != "none") {
    get_logger(LogLayer::Application)->warn("Unknown auth mode '{}'; defaulting to 'none'.", mode);
  }
  return protocol::AuthMode::None;
}

bool workflow_has_content(const WorkflowConfig& workflow_config) {
  return !workflow_config.actors.empty() || !workflow_config.tasks.empty();
}

YAML::Node find_application_node(const YAML::Node& root) {
  if (root[kApplicationSectionName] && root[kApplicationSectionName].IsMap()) {
    return root[kApplicationSectionName];
  }
  return root;
}

YAML::Node find_workflow_node(const YAML::Node& root) {
  if (root[kWorkflowSectionName] && root[kWorkflowSectionName].IsMap()) {
    return root[kWorkflowSectionName];
  }
  return root;
}

YAML::Node find_interface_node(const YAML::Node& launch_node, const char* interface_name) {
  if (!launch_node || !launch_node.IsMap()) {
    return {};
  }
  if (launch_node["interfaces"] && launch_node["interfaces"].IsMap() && launch_node["interfaces"][interface_name]) {
    return launch_node["interfaces"][interface_name];
  }
  if (launch_node[interface_name]) {
    return launch_node[interface_name];
  }
  return {};
}

bool interface_enabled(const YAML::Node& interface_node) {
  if (!interface_node || !interface_node.IsMap()) {
    return false;
  }
  if (interface_node["enabled"]) {
    return interface_node["enabled"].as<bool>();
  }
  return interface_node.size() > 0;
}

bool tls_enabled_by_config(const protocol::TlsServerConfig& tls_config) {
  return tls_config.identity.configured() || tls_config.client_trust.root_certificates.configured();
}

RequestFileKind parse_request_file_kind(const YAML::Node& request_node) {
  if (!request_node || !request_node.IsMap() || !(request_node["kind"] || request_node["type"])) {
    return RequestFileKind::None;
  }

  const std::string kind =
      request_node["kind"] ? request_node["kind"].as<std::string>() : request_node["type"].as<std::string>();
  if (kind == "workflow_yaml" || kind == "yaml") {
    return RequestFileKind::WorkflowYaml;
  }
  if (kind == "workflow_text" || kind == "text") {
    return RequestFileKind::WorkflowText;
  }
  get_logger(LogLayer::Application)->warn("Unknown request file kind '{}'; ignoring request config.", kind);
  return RequestFileKind::None;
}

RequestFileConfig parse_request_file_config(const YAML::Node& request_node) {
  RequestFileConfig request_config;
  if (!request_node || !request_node.IsMap()) {
    return request_config;
  }

  request_config.kind = parse_request_file_kind(request_node);
  if (request_node["path"]) {
    request_config.path = request_node["path"].as<std::string>();
  }
  return request_config;
}

WorkflowConfig parse_workflow_yaml(const YAML::Node& root) {
  WorkflowConfig workflow_config;
  if (!root || !root.IsMap()) {
    return workflow_config;
  }

  if (root["id"]) {
    workflow_config.id = root["id"].as<std::string>();
  }
  if (workflow_config.id.empty()) {
    workflow_config.id = kDefaultWorkflowId;
  }

  if (root["optimization"] && root["optimization"].IsMap()) {
    const YAML::Node optimization = root["optimization"];
    if (optimization["backend"]) {
      workflow_config.optimization.backend = optimization["backend"].as<std::string>();
    }
    if (optimization["time_limit_ms"]) {
      workflow_config.optimization.time_limit_ms = optimization["time_limit_ms"].as<int64_t>();
    }
    if (optimization["relative_gap_limit"]) {
      workflow_config.optimization.relative_gap_limit = optimization["relative_gap_limit"].as<double>();
    }
    if (optimization["num_search_workers"]) {
      workflow_config.optimization.num_search_workers = optimization["num_search_workers"].as<int>();
    }
    if (optimization["allow_partial_plan"]) {
      workflow_config.optimization.allow_partial_plan = optimization["allow_partial_plan"].as<bool>();
    }
    if (optimization["objective"] && optimization["objective"].IsMap()) {
      const YAML::Node objective = optimization["objective"];
      if (objective["fulfilled_task_weight"]) {
        workflow_config.optimization.objective.fulfilled_task_weight = objective["fulfilled_task_weight"].as<int64_t>();
      }
      if (objective["priority_weight"]) {
        workflow_config.optimization.objective.priority_weight = objective["priority_weight"].as<int64_t>();
      }
      if (objective["makespan_weight"]) {
        workflow_config.optimization.objective.makespan_weight = objective["makespan_weight"].as<int64_t>();
      }
      if (objective["travel_distance_weight"]) {
        workflow_config.optimization.objective.travel_distance_weight =
            objective["travel_distance_weight"].as<int64_t>();
      }
      if (objective["tardiness_weight"]) {
        workflow_config.optimization.objective.tardiness_weight = objective["tardiness_weight"].as<int64_t>();
      }
      if (objective["execution_cost_weight"]) {
        workflow_config.optimization.objective.execution_cost_weight = objective["execution_cost_weight"].as<int64_t>();
      }
      if (objective["preferred_actor_weight"]) {
        workflow_config.optimization.objective.preferred_actor_weight =
            objective["preferred_actor_weight"].as<int64_t>();
      }
    }
  }

  if (root["actors"] && root["actors"].IsSequence()) {
    const YAML::Node actor_nodes = root["actors"];
    workflow_config.actors.reserve(actor_nodes.size());
    for (const YAML::Node& actor_node : actor_nodes) {
      ActorConfig actor_config;
      if (actor_node["id"]) {
        actor_config.id = actor_node["id"].as<std::string>();
      }
      if (actor_node["type"]) {
        actor_config.type = actor_node["type"].as<std::string>();
      }
      if (actor_node["capacity"]) {
        actor_config.capacity = actor_node["capacity"].as<int>();
      }
      append_transformed_sequence(actor_node["capabilities"],
                                  actor_config.capabilities,
                                  [](const YAML::Node& capability_node) { return capability_node.as<std::string>(); });
      if (actor_node["execution_cost_per_unit"]) {
        actor_config.execution_cost_per_unit = actor_node["execution_cost_per_unit"].as<double>();
      }
      append_transformed_sequence(actor_node["windows"], actor_config.windows, parse_availability_window);
      if (actor_config.windows.empty()) {
        actor_config.windows.push_back(
            {.start = kDefaultAvailabilityWindowStart, .end = kDefaultAvailabilityWindowEnd});
      }
      workflow_config.actors.push_back(std::move(actor_config));
    }
  }

  if (root["tasks"] && root["tasks"].IsSequence()) {
    const YAML::Node task_nodes = root["tasks"];
    workflow_config.tasks.reserve(task_nodes.size());
    for (const YAML::Node& task_node : task_nodes) {
      TaskConfig task_config;
      if (task_node["id"]) {
        task_config.id = task_node["id"].as<std::string>();
      }
      if (task_node["requested_time"]) {
        task_config.requested_time = static_cast<Time>(task_node["requested_time"].as<int64_t>());
      }
      if (task_node["duration"]) {
        task_config.duration = static_cast<Duration>(task_node["duration"].as<int64_t>());
      }
      if (task_node["latest_start_time"]) {
        task_config.latest_start_time = static_cast<Time>(task_node["latest_start_time"].as<int64_t>());
      }
      if (task_node["deadline"]) {
        task_config.deadline = static_cast<Time>(task_node["deadline"].as<int64_t>());
      }
      if (task_node["priority"]) {
        task_config.priority = task_node["priority"].as<Priority>();
      }
      if (task_node["demand"]) {
        task_config.demand = task_node["demand"].as<int>();
      }
      if (task_node["mandatory"]) {
        task_config.mandatory = task_node["mandatory"].as<bool>();
      }
      if (task_node["preemptible"]) {
        task_config.preemptible = task_node["preemptible"].as<bool>();
      }
      append_transformed_sequence(task_node["allowed_actor_types"],
                                  task_config.allowed_actor_types,
                                  [](const YAML::Node& type_node) { return type_node.as<std::string>(); });
      append_transformed_sequence(task_node["allowed_actor_ids"],
                                  task_config.allowed_actor_ids,
                                  [](const YAML::Node& actor_id_node) { return actor_id_node.as<std::string>(); });
      append_transformed_sequence(task_node["preferred_actor_ids"],
                                  task_config.preferred_actor_ids,
                                  [](const YAML::Node& actor_id_node) { return actor_id_node.as<std::string>(); });
      append_transformed_sequence(task_node["required_capabilities"],
                                  task_config.required_capabilities,
                                  [](const YAML::Node& capability_node) { return capability_node.as<std::string>(); });
      append_transformed_sequence(task_node["dependency_task_ids"],
                                  task_config.dependency_task_ids,
                                  [](const YAML::Node& dependency_node) { return dependency_node.as<std::string>(); });
      append_transformed_sequence(task_node["mutually_exclusive_task_ids"],
                                  task_config.mutually_exclusive_task_ids,
                                  [](const YAML::Node& task_id_node) { return task_id_node.as<std::string>(); });
      append_actor_distance_overrides(task_node["actor_distances"], task_config.actor_distances);
      if (task_node["tardiness_cost_per_unit"]) {
        task_config.tardiness_cost_per_unit = task_node["tardiness_cost_per_unit"].as<double>();
      }
      if (task_node["early_start_bonus"]) {
        task_config.early_start_bonus = task_node["early_start_bonus"].as<double>();
      }
      append_transformed_sequence(
          task_node["phase_durations"], task_config.phase_durations, [](const YAML::Node& duration_node) {
            return static_cast<Duration>(duration_node.as<int64_t>());
          });
      if (task_config.deadline == 0) {
        task_config.deadline = task_config.requested_time + task_config.duration + kDefaultDeadlineSlack;
      }
      workflow_config.tasks.push_back(std::move(task_config));
    }
  }

  return workflow_config;
}

ApplicationLaunchConfig parse_service_yaml(const YAML::Node& service_node, const YAML::Node& root) {
  ApplicationLaunchConfig launch_config;
  if (!service_node || !service_node.IsMap()) {
    return launch_config;
  }

  launch_config.configured = true;
  const YAML::Node security_node =
      service_node["security"] && service_node["security"].IsMap() ? service_node["security"] : root["security"];

  launch_config.security.mode = parse_auth_mode(security_node);
  if (security_node && security_node.IsMap()) {
    if (security_node["expected_credential"]) {
      launch_config.security.expected_credential = security_node["expected_credential"].as<std::string>();
    }
    if (security_node["require_secure_transport"]) {
      launch_config.security.require_secure_transport = security_node["require_secure_transport"].as<bool>();
    }
  }

  if (const YAML::Node cli_node = find_interface_node(service_node, "cli")) {
    launch_config.interfaces.cli.enabled = interface_enabled(cli_node);
    if (cli_node["prompt"]) {
      launch_config.interfaces.cli.prompt = cli_node["prompt"].as<std::string>();
    }
  }

  if (const YAML::Node http_node = find_interface_node(service_node, "http")) {
    launch_config.interfaces.http.enabled = interface_enabled(http_node);
    if (http_node["bind_address"]) {
      launch_config.interfaces.http.endpoint.bind_address = http_node["bind_address"].as<std::string>();
    }
    if (http_node["port"]) {
      launch_config.interfaces.http.endpoint.port = http_node["port"].as<int>();
    }
    launch_config.interfaces.http.endpoint.tls = parse_tls_server_config(http_node["tls"]);
    if (http_node["use_tls"]) {
      launch_config.interfaces.http.endpoint.use_tls = http_node["use_tls"].as<bool>();
    } else {
      launch_config.interfaces.http.endpoint.use_tls =
          tls_enabled_by_config(launch_config.interfaces.http.endpoint.tls);
    }
    if (http_node["io_threads"]) {
      launch_config.interfaces.http.endpoint.io_threads = http_node["io_threads"].as<std::size_t>();
    }
    if (http_node["max_body_bytes"]) {
      launch_config.interfaces.http.endpoint.max_body_bytes = http_node["max_body_bytes"].as<std::size_t>();
    }
  }

  if (const YAML::Node grpc_node = find_interface_node(service_node, "grpc")) {
    launch_config.interfaces.grpc.enabled = interface_enabled(grpc_node);
    if (grpc_node["bind_address"]) {
      launch_config.interfaces.grpc.endpoint.bind_address = grpc_node["bind_address"].as<std::string>();
    }
    if (grpc_node["port"]) {
      launch_config.interfaces.grpc.endpoint.port = grpc_node["port"].as<int>();
    }
    launch_config.interfaces.grpc.endpoint.tls = parse_tls_server_config(grpc_node["tls"]);
    if (grpc_node["use_tls"]) {
      launch_config.interfaces.grpc.endpoint.use_tls = grpc_node["use_tls"].as<bool>();
    } else {
      launch_config.interfaces.grpc.endpoint.use_tls =
          tls_enabled_by_config(launch_config.interfaces.grpc.endpoint.tls);
    }
    if (grpc_node["completion_queue_threads"]) {
      launch_config.interfaces.grpc.endpoint.completion_queue_threads =
          grpc_node["completion_queue_threads"].as<std::size_t>();
    }
    if (grpc_node["max_receive_message_bytes"]) {
      launch_config.interfaces.grpc.endpoint.max_receive_message_bytes =
          grpc_node["max_receive_message_bytes"].as<int>();
    }
    if (grpc_node["max_send_message_bytes"]) {
      launch_config.interfaces.grpc.endpoint.max_send_message_bytes = grpc_node["max_send_message_bytes"].as<int>();
    }
  }

  const YAML::Node control_plane_node =
      service_node[kControlPlaneSectionName] && service_node[kControlPlaneSectionName].IsMap()
          ? service_node[kControlPlaneSectionName]
          : root[kControlPlaneSectionName];
  if (control_plane_node && control_plane_node.IsMap()) {
    if (control_plane_node["enabled"]) {
      launch_config.control_plane.enabled = control_plane_node["enabled"].as<bool>();
    } else {
      launch_config.control_plane.enabled = control_plane_node.size() > 0;
    }
    if (control_plane_node["database_path"]) {
      launch_config.control_plane.database_path = control_plane_node["database_path"].as<std::string>();
    } else if (control_plane_node["state_directory"]) {
      launch_config.control_plane.database_path = control_plane_node["state_directory"].as<std::string>();
    }
    if (control_plane_node["recover_on_start"]) {
      launch_config.control_plane.recover_on_start = control_plane_node["recover_on_start"].as<bool>();
    }
    if (control_plane_node["prune_after_days"]) {
      launch_config.control_plane.prune_after_days = control_plane_node["prune_after_days"].as<std::int32_t>();
    }
  }

  const WorkflowConfig bootstrap_workflow = parse_workflow_yaml(find_workflow_node(root));
  if (workflow_has_content(bootstrap_workflow)) {
    launch_config.bootstrap_workflow = bootstrap_workflow;
  }

  return launch_config;
}

ApplicationLaunchConfig parse_launch_yaml(const YAML::Node& root) {
  if (!root || !root.IsMap() || !root[kLaunchSectionName] || !root[kLaunchSectionName].IsMap()) {
    return {};
  }
  return parse_service_yaml(root[kLaunchSectionName], root);
}

ApplicationConfig parse_application_yaml(const YAML::Node& root) {
  ApplicationConfig application_config;
  if (!root || !root.IsMap()) {
    return application_config;
  }

  const YAML::Node application_node = find_application_node(root);
  if (!application_node || !application_node.IsMap()) {
    return application_config;
  }

  application_config.configured =
      static_cast<bool>(root[kApplicationSectionName]) || static_cast<bool>(application_node["mode"]) ||
      static_cast<bool>(application_node["request"]) || static_cast<bool>(application_node[kServiceSectionName]);
  if (!application_config.configured) {
    return application_config;
  }

  const std::string mode = application_node["mode"] ? application_node["mode"].as<std::string>() : "one_shot";
  if (mode == "serve") {
    application_config.mode = ApplicationMode::Serve;
  } else if (mode == "one_shot") {
    application_config.mode = ApplicationMode::OneShot;
  } else {
    get_logger(LogLayer::Application)->warn("Unknown application mode '{}'; defaulting to 'one_shot'.", mode);
    application_config.mode = ApplicationMode::OneShot;
  }

  application_config.request = parse_request_file_config(application_node["request"]);
  const YAML::Node service_node = application_node[kServiceSectionName] && application_node[kServiceSectionName].IsMap()
                                      ? application_node[kServiceSectionName]
                                      : application_node;
  application_config.service = parse_service_yaml(service_node, application_node);

  const YAML::Node bootstrap_request_node =
      service_node["bootstrap_request"] && service_node["bootstrap_request"].IsMap()
          ? service_node["bootstrap_request"]
          : application_node["bootstrap_request"];
  application_config.service.bootstrap_request = parse_request_file_config(bootstrap_request_node);
  if (application_config.service.bootstrap_request.configured()) {
    application_config.service.bootstrap_workflow.reset();
    application_config.service.configured = true;
  }

  return application_config;
}

}  // namespace

WorkflowConfig load_config_from_string(const std::string& content) {
  try {
    return parse_workflow_yaml(find_workflow_node(YAML::Load(content)));
  } catch (const YAML::Exception&) {
    get_logger(LogLayer::Application)->error("Failed to parse workflow config from string.");
    return {};
  }
}

WorkflowConfig load_config_from_file(const char* path) {
  try {
    return parse_workflow_yaml(find_workflow_node(YAML::LoadFile(path)));
  } catch (const YAML::Exception&) {
    get_logger(LogLayer::Application)->error("Failed to parse workflow config file '{}'.", path);
    return {};
  }
}

ApplicationLaunchConfig load_launch_config_from_string(const std::string& content) {
  try {
    return parse_launch_yaml(YAML::Load(content));
  } catch (const YAML::Exception&) {
    get_logger(LogLayer::Application)->error("Failed to parse application launch config from string.");
    return {};
  }
}

ApplicationLaunchConfig load_launch_config_from_file(const char* path) {
  try {
    return parse_launch_yaml(YAML::LoadFile(path));
  } catch (const YAML::Exception&) {
    get_logger(LogLayer::Application)->error("Failed to parse application launch config file '{}'.", path);
    return {};
  }
}

ApplicationConfig load_application_config_from_string(const std::string& content) {
  try {
    return parse_application_yaml(YAML::Load(content));
  } catch (const YAML::Exception&) {
    get_logger(LogLayer::Application)->error("Failed to parse application config from string.");
    return {};
  }
}

ApplicationConfig load_application_config_from_file(const char* path) {
  try {
    return parse_application_yaml(YAML::LoadFile(path));
  } catch (const YAML::Exception&) {
    get_logger(LogLayer::Application)->error("Failed to parse application config file '{}'.", path);
    return {};
  }
}

}  // namespace task_orchestrator::app
