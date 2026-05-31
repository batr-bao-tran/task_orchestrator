#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "config/config.hpp"

namespace {
namespace to = task_orchestrator;

std::filesystem::path write_temp_yaml_file(const std::string& file_name, const std::string& contents) {
  const std::filesystem::path file_path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream output(file_path);
  output << contents;
  output.close();
  return file_path;
}

std::filesystem::path runfiles_path(std::string_view relative_path) {
  const char* const test_srcdir = std::getenv("TEST_SRCDIR");
  const char* const test_workspace = std::getenv("TEST_WORKSPACE");
  EXPECT_NE(nullptr, test_srcdir);
  EXPECT_NE(nullptr, test_workspace);
  return std::filesystem::path(test_srcdir) / test_workspace / relative_path;
}

TEST(ConfigLoaderTest, ParseYaml) {
  std::string content = R"(
id: w1
optimization:
  backend: indexed_exact
  time_limit_ms: 2500
  relative_gap_limit: 0.05
  num_search_workers: 4
  allow_partial_plan: false
  objective:
    fulfilled_task_weight: 1234
    priority_weight: 99
    makespan_weight: -7
    travel_distance_weight: -3
    tardiness_weight: -11
    execution_cost_weight: -13
    preferred_actor_weight: 17
actors:
  - id: a1
    type: robot
    capacity: 2
    capabilities: [scanner, forklift]
    execution_cost_per_unit: 1.5
    windows:
      - start: 0
        end: 100
  - id: a2
    type: machine
    capacity: 1
    windows:
      - start: 0
        end: 50
      - start: 60
        end: 100
tasks:
  - id: t1
    requested_time: 0
    duration: 10
    latest_start_time: 12
    deadline: 20
    priority: 3
    demand: 2
    mandatory: false
    preemptible: false
    allowed_actor_types: [robot]
    allowed_actor_ids: [a1]
    required_capabilities: [scanner]
    mutually_exclusive_task_ids: [t2]
    tardiness_cost_per_unit: 1.25
    early_start_bonus: 0.75
  - id: t2
    requested_time: 5
    duration: 15
    deadline: 30
    allowed_actor_types: [robot, machine]
    preferred_actor_ids: [a2]
    dependency_task_ids: [t1]
    actor_distances:
      a1: 5
      a2: 1
)";
  const to::app::WorkflowConfig cfg = to::app::load_config_from_string(content);
  EXPECT_EQ(cfg.id, "w1");
  EXPECT_EQ(cfg.optimization.backend, "indexed_exact");
  EXPECT_EQ(cfg.optimization.time_limit_ms, 2500);
  EXPECT_DOUBLE_EQ(cfg.optimization.relative_gap_limit, 0.05);
  EXPECT_EQ(cfg.optimization.num_search_workers, 4);
  EXPECT_FALSE(cfg.optimization.allow_partial_plan);
  EXPECT_EQ(cfg.optimization.objective.fulfilled_task_weight, 1234);
  EXPECT_EQ(cfg.optimization.objective.priority_weight, 99);
  EXPECT_EQ(cfg.optimization.objective.makespan_weight, -7);
  EXPECT_EQ(cfg.optimization.objective.travel_distance_weight, -3);
  EXPECT_EQ(cfg.optimization.objective.tardiness_weight, -11);
  EXPECT_EQ(cfg.optimization.objective.execution_cost_weight, -13);
  EXPECT_EQ(cfg.optimization.objective.preferred_actor_weight, 17);
  ASSERT_EQ(cfg.actors.size(), 2U);
  EXPECT_EQ(cfg.actors[0].id, "a1");
  EXPECT_EQ(cfg.actors[0].type, "robot");
  EXPECT_EQ(cfg.actors[0].capacity, 2);
  ASSERT_EQ(cfg.actors[0].capabilities.size(), 2U);
  EXPECT_EQ(cfg.actors[0].capabilities[0], "scanner");
  EXPECT_DOUBLE_EQ(cfg.actors[0].execution_cost_per_unit, 1.5);
  ASSERT_EQ(cfg.actors[0].windows.size(), 1U);
  EXPECT_EQ(cfg.actors[0].windows[0].start, 0);
  EXPECT_EQ(cfg.actors[0].windows[0].end, 100);

  EXPECT_EQ(cfg.actors[1].id, "a2");
  ASSERT_EQ(cfg.actors[1].windows.size(), 2U);
  EXPECT_EQ(cfg.actors[1].windows[1].start, 60);
  EXPECT_EQ(cfg.actors[1].windows[1].end, 100);

  ASSERT_EQ(cfg.tasks.size(), 2U);
  EXPECT_EQ(cfg.tasks[0].id, "t1");
  EXPECT_EQ(cfg.tasks[0].requested_time, 0);
  EXPECT_EQ(cfg.tasks[0].duration, 10);
  EXPECT_EQ(cfg.tasks[0].latest_start_time, 12);
  EXPECT_EQ(cfg.tasks[0].deadline, 20);
  EXPECT_EQ(cfg.tasks[0].priority, 3);
  EXPECT_EQ(cfg.tasks[0].demand, 2);
  EXPECT_FALSE(cfg.tasks[0].mandatory);
  EXPECT_FALSE(cfg.tasks[0].preemptible);
  ASSERT_EQ(cfg.tasks[0].allowed_actor_types.size(), 1U);
  EXPECT_EQ(cfg.tasks[0].allowed_actor_types[0], "robot");
  ASSERT_EQ(cfg.tasks[0].allowed_actor_ids.size(), 1U);
  EXPECT_EQ(cfg.tasks[0].allowed_actor_ids[0], "a1");
  ASSERT_EQ(cfg.tasks[0].required_capabilities.size(), 1U);
  EXPECT_EQ(cfg.tasks[0].required_capabilities[0], "scanner");
  ASSERT_EQ(cfg.tasks[0].mutually_exclusive_task_ids.size(), 1U);
  EXPECT_EQ(cfg.tasks[0].mutually_exclusive_task_ids[0], "t2");
  EXPECT_DOUBLE_EQ(cfg.tasks[0].tardiness_cost_per_unit, 1.25);
  EXPECT_DOUBLE_EQ(cfg.tasks[0].early_start_bonus, 0.75);

  EXPECT_EQ(cfg.tasks[1].allowed_actor_types.size(), 2U);
  ASSERT_EQ(cfg.tasks[1].preferred_actor_ids.size(), 1U);
  EXPECT_EQ(cfg.tasks[1].preferred_actor_ids[0], "a2");
  ASSERT_EQ(cfg.tasks[1].dependency_task_ids.size(), 1U);
  EXPECT_EQ(cfg.tasks[1].dependency_task_ids[0], "t1");
  EXPECT_EQ(cfg.tasks[1].actor_distances.size(), 2U);
  EXPECT_EQ(cfg.tasks[1].actor_distances.at("a2"), 1);
}

TEST(ConfigLoaderTest, AppliesDefaultsForMissingFields) {
  const to::app::WorkflowConfig cfg = to::app::load_config_from_string(R"(
actors:
  - id: a1
    type: robot
tasks:
  - id: t1
    requested_time: 5
    duration: 7
)");

  EXPECT_EQ("workflow", cfg.id);
  ASSERT_EQ(1U, cfg.actors.size());
  ASSERT_EQ(1U, cfg.actors[0].windows.size());
  EXPECT_EQ(0, cfg.actors[0].windows[0].start);
  EXPECT_EQ(1000000, cfg.actors[0].windows[0].end);

  ASSERT_EQ(1U, cfg.tasks.size());
  EXPECT_EQ(1012, cfg.tasks[0].deadline);
  EXPECT_EQ("auto", cfg.optimization.backend);
  EXPECT_TRUE(cfg.optimization.allow_partial_plan);
}

TEST(ConfigLoaderTest, ParseWorkflowNestedUnderWorkflowSection) {
  const to::app::WorkflowConfig cfg = to::app::load_config_from_string(R"(
launch:
  interfaces:
    cli:
      enabled: true
workflow:
  id: nested_workflow
  actors:
    - id: a1
      type: robot
  tasks:
    - id: t1
      requested_time: 0
      duration: 3
)");

  EXPECT_EQ("nested_workflow", cfg.id);
  ASSERT_EQ(1U, cfg.actors.size());
  ASSERT_EQ(1U, cfg.tasks.size());
}

TEST(ConfigLoaderTest, ParseLaunchConfig) {
  const to::app::ApplicationLaunchConfig launch_config = to::app::load_launch_config_from_string(R"(
launch:
  security:
    mode: bearer_token
    expected_credential: super-secret
    require_secure_transport: true
  interfaces:
    cli:
      prompt: "planner> "
    http:
      bind_address: 0.0.0.0
      port: 8181
      io_threads: 6
      max_body_bytes: 2048
      tls:
        identity:
          certificate_chain:
            file: /tmp/server-cert.pem
          private_key:
            inline_pem: dummy-key
    grpc:
      enabled: true
      bind_address: 127.0.0.1
      port: 9191
      completion_queue_threads: 3
      max_receive_message_bytes: 4096
      max_send_message_bytes: 8192
  control_plane:
    enabled: true
    database_path: /var/lib/task-orchestrator/control-plane/control_plane.sqlite3
    recover_on_start: false
    prune_after_days: 21
workflow:
  id: bootstrap
  actors:
    - id: a1
      type: robot
  tasks:
    - id: t1
      requested_time: 0
      duration: 1
)");

  EXPECT_TRUE(launch_config.configured);
  EXPECT_TRUE(launch_config.has_enabled_interface());
  EXPECT_EQ(to::protocol::AuthMode::BearerToken, launch_config.security.mode);
  EXPECT_EQ("super-secret", launch_config.security.expected_credential);
  EXPECT_TRUE(launch_config.security.require_secure_transport);

  EXPECT_TRUE(launch_config.interfaces.cli.enabled);
  EXPECT_EQ("planner> ", launch_config.interfaces.cli.prompt);

  EXPECT_TRUE(launch_config.interfaces.http.enabled);
  EXPECT_EQ("0.0.0.0", launch_config.interfaces.http.endpoint.bind_address);
  EXPECT_EQ(8181, launch_config.interfaces.http.endpoint.port);
  EXPECT_TRUE(launch_config.interfaces.http.endpoint.use_tls);
  EXPECT_EQ(6U, launch_config.interfaces.http.endpoint.io_threads);
  EXPECT_EQ(2048U, launch_config.interfaces.http.endpoint.max_body_bytes);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::FilePath,
            launch_config.interfaces.http.endpoint.tls.identity.certificate_chain.kind);
  EXPECT_EQ("/tmp/server-cert.pem", launch_config.interfaces.http.endpoint.tls.identity.certificate_chain.value);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::InlinePem,
            launch_config.interfaces.http.endpoint.tls.identity.private_key.kind);

  EXPECT_TRUE(launch_config.interfaces.grpc.enabled);
  EXPECT_EQ(9191, launch_config.interfaces.grpc.endpoint.port);
  EXPECT_EQ(3U, launch_config.interfaces.grpc.endpoint.completion_queue_threads);
  EXPECT_EQ(4096, launch_config.interfaces.grpc.endpoint.max_receive_message_bytes);
  EXPECT_EQ(8192, launch_config.interfaces.grpc.endpoint.max_send_message_bytes);
  EXPECT_TRUE(launch_config.control_plane.enabled);
  EXPECT_EQ("/var/lib/task-orchestrator/control-plane/control_plane.sqlite3",
            launch_config.control_plane.database_path);
  EXPECT_FALSE(launch_config.control_plane.recover_on_start);
  EXPECT_EQ(21, launch_config.control_plane.prune_after_days);

  ASSERT_TRUE(launch_config.bootstrap_workflow.has_value());
  EXPECT_EQ("bootstrap", launch_config.bootstrap_workflow->id);
  ASSERT_EQ(1U, launch_config.bootstrap_workflow->actors.size());
  ASSERT_EQ(1U, launch_config.bootstrap_workflow->tasks.size());
}

TEST(ConfigLoaderTest, ParseLaunchConfigSupportsTlsSourceVariantsAndTopLevelInterfaces) {
  const to::app::ApplicationLaunchConfig launch_config = to::app::load_launch_config_from_string(R"(
security:
  mode: none
  require_secure_transport: true
launch:
  cli:
    prompt: "fallback> "
  http:
    bind_address: 127.0.0.2
    port: 8282
    use_tls: false
    io_threads: 2
    max_body_bytes: 1024
  grpc:
    bind_address: 127.0.0.3
    port: 9393
    tls:
      identity:
        certificate_chain: /tmp/server-cert.pem
        private_key:
          file: /tmp/server-key.pem
        private_key_password:
          inline_pem: secret-password
      client_trust:
        root_certificates:
          inline_pem: root-pem
        use_system_default_roots: true
        verify_peer: false
        expected_peer_name: peer.internal
      require_client_certificate: true
)");

  EXPECT_TRUE(launch_config.configured);
  EXPECT_EQ(to::protocol::AuthMode::None, launch_config.security.mode);
  EXPECT_TRUE(launch_config.security.require_secure_transport);

  EXPECT_TRUE(launch_config.interfaces.cli.enabled);
  EXPECT_EQ("fallback> ", launch_config.interfaces.cli.prompt);

  EXPECT_TRUE(launch_config.interfaces.http.enabled);
  EXPECT_EQ("127.0.0.2", launch_config.interfaces.http.endpoint.bind_address);
  EXPECT_EQ(8282, launch_config.interfaces.http.endpoint.port);
  EXPECT_FALSE(launch_config.interfaces.http.endpoint.use_tls);
  EXPECT_EQ(2U, launch_config.interfaces.http.endpoint.io_threads);
  EXPECT_EQ(1024U, launch_config.interfaces.http.endpoint.max_body_bytes);

  EXPECT_TRUE(launch_config.interfaces.grpc.enabled);
  EXPECT_EQ("127.0.0.3", launch_config.interfaces.grpc.endpoint.bind_address);
  EXPECT_EQ(9393, launch_config.interfaces.grpc.endpoint.port);
  EXPECT_TRUE(launch_config.interfaces.grpc.endpoint.use_tls);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::FilePath,
            launch_config.interfaces.grpc.endpoint.tls.identity.certificate_chain.kind);
  EXPECT_EQ("/tmp/server-cert.pem", launch_config.interfaces.grpc.endpoint.tls.identity.certificate_chain.value);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::FilePath,
            launch_config.interfaces.grpc.endpoint.tls.identity.private_key.kind);
  EXPECT_EQ("/tmp/server-key.pem", launch_config.interfaces.grpc.endpoint.tls.identity.private_key.value);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::InlinePem,
            launch_config.interfaces.grpc.endpoint.tls.identity.private_key_password.kind);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::InlinePem,
            launch_config.interfaces.grpc.endpoint.tls.client_trust.root_certificates.kind);
  EXPECT_TRUE(launch_config.interfaces.grpc.endpoint.tls.client_trust.use_system_default_roots);
  EXPECT_FALSE(launch_config.interfaces.grpc.endpoint.tls.client_trust.verify_peer);
  EXPECT_EQ("peer.internal", launch_config.interfaces.grpc.endpoint.tls.client_trust.expected_peer_name);
  EXPECT_TRUE(launch_config.interfaces.grpc.endpoint.tls.require_client_certificate);
  EXPECT_FALSE(launch_config.bootstrap_workflow.has_value());
}

TEST(ConfigLoaderTest, ParseApplicationConfigForOneShotMode) {
  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
application:
  mode: one_shot
  request:
    kind: workflow_text
    path: requests/demo.txt
)");

  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::ApplicationMode::OneShot, config.mode);
  EXPECT_EQ(to::app::RequestFileKind::WorkflowText, config.request.kind);
  EXPECT_EQ("requests/demo.txt", config.request.path);
  EXPECT_TRUE(config.valid());
}

TEST(ConfigLoaderTest, ParseApplicationConfigSupportsRootFallbackAliases) {
  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
mode: serve
request:
  type: text
  path: requests/demo.txt
bootstrap_request:
  type: yaml
  path: bootstrap.yaml
security:
  mode: unsupported_mode
cli:
  prompt: "runtime> "
http:
  bind_address: 127.0.0.1
  port: 8081
)");

  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::ApplicationMode::Serve, config.mode);
  EXPECT_EQ(to::app::RequestFileKind::WorkflowText, config.request.kind);
  EXPECT_EQ("requests/demo.txt", config.request.path);
  EXPECT_EQ(to::protocol::AuthMode::None, config.service.security.mode);
  EXPECT_TRUE(config.service.interfaces.cli.enabled);
  EXPECT_EQ("runtime> ", config.service.interfaces.cli.prompt);
  EXPECT_TRUE(config.service.interfaces.http.enabled);
  EXPECT_EQ(8081, config.service.interfaces.http.endpoint.port);
  EXPECT_EQ(to::app::RequestFileKind::WorkflowYaml, config.service.bootstrap_request.kind);
  EXPECT_EQ("bootstrap.yaml", config.service.bootstrap_request.path);
  EXPECT_FALSE(config.service.bootstrap_workflow.has_value());
  EXPECT_TRUE(config.valid());
}

TEST(ConfigLoaderTest, ParseApplicationConfigForServeMode) {
  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
application:
  mode: serve
  service:
    security:
      mode: api_key
      expected_credential: local-dev-key
    bootstrap_request:
      kind: workflow_yaml
      path: bootstrap.yaml
    interfaces:
      cli:
        enabled: true
      grpc:
        enabled: true
        port: 9191
)");

  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::ApplicationMode::Serve, config.mode);
  EXPECT_EQ(to::protocol::AuthMode::ApiKey, config.service.security.mode);
  EXPECT_EQ("local-dev-key", config.service.security.expected_credential);
  EXPECT_EQ(to::app::RequestFileKind::WorkflowYaml, config.service.bootstrap_request.kind);
  EXPECT_EQ("bootstrap.yaml", config.service.bootstrap_request.path);
  EXPECT_TRUE(config.service.interfaces.cli.enabled);
  EXPECT_TRUE(config.service.interfaces.grpc.enabled);
  EXPECT_EQ(9191, config.service.interfaces.grpc.endpoint.port);
  EXPECT_TRUE(config.valid());
}

TEST(ConfigLoaderTest, UnknownApplicationModeAndRequestKindDefaultGracefully) {
  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
application:
  mode: unexpected_mode
  request:
    kind: spreadsheet
    path: requests/demo.csv
  service:
    cli:
      enabled: true
)");

  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::ApplicationMode::OneShot, config.mode);
  EXPECT_EQ(to::app::RequestFileKind::None, config.request.kind);
  EXPECT_EQ("requests/demo.csv", config.request.path);
  EXPECT_TRUE(config.service.interfaces.cli.enabled);
  EXPECT_FALSE(config.valid());
}

TEST(ConfigLoaderTest, ParsesAdditionalWorkflowListsAndNonMapRootsGracefully) {
  const to::app::WorkflowConfig workflow_config = to::app::load_config_from_string(R"(
workflow:
  actors:
    - id: a1
      type: robot
      capabilities: [lift, scan]
  tasks:
    - id: t1
      requested_time: 3
      duration: 4
      required_capabilities: [scan]
      dependency_task_ids: [prep]
      mutually_exclusive_task_ids: [charge]
      phase_durations: [1, 3]
)");

  ASSERT_EQ(1U, workflow_config.actors.size());
  EXPECT_EQ(std::vector<std::string>({"lift", "scan"}), workflow_config.actors.front().capabilities);
  ASSERT_EQ(1U, workflow_config.tasks.size());
  EXPECT_EQ(std::vector<std::string>({"scan"}), workflow_config.tasks.front().required_capabilities);
  EXPECT_EQ(std::vector<std::string>({"prep"}), workflow_config.tasks.front().dependency_task_ids);
  EXPECT_EQ(std::vector<std::string>({"charge"}), workflow_config.tasks.front().mutually_exclusive_task_ids);
  EXPECT_EQ(std::vector<to::Duration>({1, 3}), workflow_config.tasks.front().phase_durations);

  const to::app::WorkflowConfig empty_workflow = to::app::load_config_from_string("- item");
  EXPECT_TRUE(empty_workflow.id.empty());
  EXPECT_TRUE(empty_workflow.actors.empty());
  EXPECT_TRUE(empty_workflow.tasks.empty());

  const to::app::ApplicationLaunchConfig empty_launch = to::app::load_launch_config_from_string("launch: 7");
  EXPECT_FALSE(empty_launch.configured);
  EXPECT_FALSE(empty_launch.has_enabled_interface());

  const to::app::ApplicationConfig empty_application = to::app::load_application_config_from_string("[]");
  EXPECT_FALSE(empty_application.configured);
  EXPECT_FALSE(empty_application.valid());

  const to::app::ApplicationConfig unconfigured_application =
      to::app::load_application_config_from_string("metadata: 1");
  EXPECT_FALSE(unconfigured_application.configured);
  EXPECT_FALSE(unconfigured_application.valid());
}

TEST(ConfigLoaderTest, ParserIgnoresUnsupportedTlsNodesAndNonMapRequests) {
  const to::app::ApplicationLaunchConfig launch_config = to::app::load_launch_config_from_string(R"(
launch:
  interfaces:
    http:
      enabled: true
      tls:
        identity:
          certificate_chain: /tmp/server-cert.pem
          private_key: [unexpected]
          private_key_password:
            unsupported: value
    grpc:
      enabled: true
      use_tls: true
      tls:
        identity: disabled
)");

  EXPECT_TRUE(launch_config.interfaces.http.enabled);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::FilePath,
            launch_config.interfaces.http.endpoint.tls.identity.certificate_chain.kind);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::None,
            launch_config.interfaces.http.endpoint.tls.identity.private_key.kind);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::None,
            launch_config.interfaces.http.endpoint.tls.identity.private_key_password.kind);
  EXPECT_TRUE(launch_config.interfaces.grpc.enabled);
  EXPECT_TRUE(launch_config.interfaces.grpc.endpoint.use_tls);
  EXPECT_EQ(to::protocol::TlsDataSourceKind::None,
            launch_config.interfaces.grpc.endpoint.tls.identity.certificate_chain.kind);

  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
application:
  mode: one_shot
  request: []
)");
  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::RequestFileKind::None, config.request.kind);
  EXPECT_FALSE(config.valid());
}

TEST(ConfigLoaderTest, ParseLaunchConfigUsesStateDirectoryAliasAndImplicitControlPlaneEnablement) {
  const to::app::ApplicationLaunchConfig launch_config = to::app::load_launch_config_from_string(R"(
launch:
  interfaces:
    cli:
      enabled: true
  control_plane:
    state_directory: /var/lib/task-orchestrator/state
)");

  EXPECT_TRUE(launch_config.configured);
  EXPECT_TRUE(launch_config.interfaces.cli.enabled);
  EXPECT_TRUE(launch_config.control_plane.enabled);
  EXPECT_EQ("/var/lib/task-orchestrator/state", launch_config.control_plane.database_path);
}

TEST(ConfigLoaderTest, MissingRequestKindDefaultsToNoneWhenPathIsPresent) {
  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
application:
  mode: one_shot
  request:
    path: requests/demo.txt
)");

  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::ApplicationMode::OneShot, config.mode);
  EXPECT_EQ(to::app::RequestFileKind::None, config.request.kind);
  EXPECT_EQ("requests/demo.txt", config.request.path);
  EXPECT_FALSE(config.valid());
}

TEST(ConfigLoaderTest, LoadersReturnEmptyConfigsForYamlExceptions) {
  const std::filesystem::path invalid_workflow_path =
      write_temp_yaml_file("task_orchestrator_invalid_workflow.yaml", "workflow: [");
  const std::filesystem::path invalid_launch_path =
      write_temp_yaml_file("task_orchestrator_invalid_launch.yaml", "launch: [");
  const std::filesystem::path invalid_application_path =
      write_temp_yaml_file("task_orchestrator_invalid_application.yaml", "application: [");

  const to::app::WorkflowConfig invalid_workflow_from_string = to::app::load_config_from_string("workflow: [");
  EXPECT_TRUE(invalid_workflow_from_string.id.empty());
  EXPECT_TRUE(invalid_workflow_from_string.actors.empty());
  EXPECT_TRUE(invalid_workflow_from_string.tasks.empty());

  const to::app::WorkflowConfig invalid_workflow_from_file =
      to::app::load_config_from_file(invalid_workflow_path.c_str());
  EXPECT_TRUE(invalid_workflow_from_file.id.empty());
  EXPECT_TRUE(invalid_workflow_from_file.actors.empty());
  EXPECT_TRUE(invalid_workflow_from_file.tasks.empty());

  const to::app::ApplicationLaunchConfig invalid_launch_from_string =
      to::app::load_launch_config_from_string("launch: [");
  EXPECT_FALSE(invalid_launch_from_string.configured);
  EXPECT_FALSE(invalid_launch_from_string.has_enabled_interface());

  const to::app::ApplicationLaunchConfig missing_launch_from_file =
      to::app::load_launch_config_from_file("/tmp/task_orchestrator_missing_launch_config.yaml");
  EXPECT_FALSE(missing_launch_from_file.configured);
  EXPECT_FALSE(missing_launch_from_file.has_enabled_interface());

  const to::app::ApplicationConfig invalid_application_from_string =
      to::app::load_application_config_from_string("application: [");
  EXPECT_FALSE(invalid_application_from_string.configured);
  EXPECT_FALSE(invalid_application_from_string.valid());

  const to::app::ApplicationConfig missing_application_from_file =
      to::app::load_application_config_from_file("/tmp/task_orchestrator_missing_application_config.yaml");
  EXPECT_FALSE(missing_application_from_file.configured);
  EXPECT_FALSE(missing_application_from_file.valid());

  const to::app::ApplicationLaunchConfig invalid_launch_from_file =
      to::app::load_launch_config_from_file(invalid_launch_path.c_str());
  EXPECT_FALSE(invalid_launch_from_file.configured);
  EXPECT_FALSE(invalid_launch_from_file.has_enabled_interface());

  const to::app::ApplicationConfig invalid_application_from_file =
      to::app::load_application_config_from_file(invalid_application_path.c_str());
  EXPECT_FALSE(invalid_application_from_file.configured);
  EXPECT_FALSE(invalid_application_from_file.valid());
}

TEST(ConfigLoaderTest, NonMapApplicationSectionFallsBackToRootApplicationParsing) {
  const to::app::ApplicationConfig config = to::app::load_application_config_from_string(R"(
application: []
)");

  EXPECT_TRUE(config.configured);
  EXPECT_EQ(to::app::ApplicationMode::OneShot, config.mode);
  EXPECT_FALSE(config.valid());
}

TEST(ConfigLoaderTest, LoadConfigFromFileReadsWorkflow) {
  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() / "task_orchestrator_config_loader_test.yaml";
  {
    std::ofstream output(temp_path);
    output << "id: from_file\nactors:\n  - id: a1\n    type: robot\ntasks:\n  - id: t1\n    requested_time: 0\n    "
              "duration: 1\n";
  }

  const to::app::WorkflowConfig cfg = to::app::load_config_from_file(temp_path.c_str());
  std::filesystem::remove(temp_path);

  EXPECT_EQ("from_file", cfg.id);
  ASSERT_EQ(1U, cfg.actors.size());
  ASSERT_EQ(1U, cfg.tasks.size());
}

TEST(ConfigLoaderTest, LoadLaunchAndApplicationConfigFromFileReadAndRejectMissingFiles) {
  const std::filesystem::path launch_path = write_temp_yaml_file("task_orchestrator_launch_config_loader_test.yaml",
                                                                 R"(
launch:
  interfaces:
    cli:
      enabled: true
      prompt: "launch> "
)");
  const to::app::ApplicationLaunchConfig launch_config = to::app::load_launch_config_from_file(launch_path.c_str());
  std::filesystem::remove(launch_path);

  EXPECT_TRUE(launch_config.configured);
  EXPECT_TRUE(launch_config.interfaces.cli.enabled);
  EXPECT_EQ("launch> ", launch_config.interfaces.cli.prompt);

  const std::filesystem::path application_path =
      write_temp_yaml_file("task_orchestrator_application_config_loader_test.yaml",
                           R"(
application:
  mode: one_shot
  request:
    kind: workflow_yaml
    path: workflows/demo.yaml
)");
  const to::app::ApplicationConfig application_config =
      to::app::load_application_config_from_file(application_path.c_str());
  std::filesystem::remove(application_path);

  EXPECT_TRUE(application_config.configured);
  EXPECT_EQ(to::app::ApplicationMode::OneShot, application_config.mode);
  EXPECT_EQ(to::app::RequestFileKind::WorkflowYaml, application_config.request.kind);
  EXPECT_EQ("workflows/demo.yaml", application_config.request.path);
  EXPECT_TRUE(application_config.valid());

  const std::filesystem::path missing_launch_path =
      std::filesystem::temp_directory_path() / "task_orchestrator_missing_launch_config.yaml";
  const to::app::ApplicationLaunchConfig missing_launch =
      to::app::load_launch_config_from_file(missing_launch_path.c_str());
  EXPECT_FALSE(missing_launch.configured);
  EXPECT_FALSE(missing_launch.has_enabled_interface());

  const std::filesystem::path missing_application_path =
      std::filesystem::temp_directory_path() / "task_orchestrator_missing_application_config.yaml";
  const to::app::ApplicationConfig missing_application =
      to::app::load_application_config_from_file(missing_application_path.c_str());
  EXPECT_FALSE(missing_application.configured);
  EXPECT_FALSE(missing_application.valid());
}

TEST(ConfigLoaderTest, ExampleApplicationConfigsLoadFromRunfiles) {
  const to::app::ApplicationConfig one_shot_config = to::app::load_application_config_from_file(
      runfiles_path("application/examples/application_configs/one_shot_text_request.yaml").c_str());
  EXPECT_TRUE(one_shot_config.configured);
  EXPECT_EQ(to::app::ApplicationMode::OneShot, one_shot_config.mode);
  EXPECT_EQ(to::app::RequestFileKind::WorkflowText, one_shot_config.request.kind);
  EXPECT_EQ("../workflow_requests/quick_pick.workflow.txt", one_shot_config.request.path);

  const to::app::ApplicationConfig serve_config = to::app::load_application_config_from_file(
      runfiles_path("application/examples/application_configs/serve_http_grpc_cli.yaml").c_str());
  EXPECT_TRUE(serve_config.configured);
  EXPECT_EQ(to::app::ApplicationMode::Serve, serve_config.mode);
  EXPECT_TRUE(serve_config.service.interfaces.cli.enabled);
  EXPECT_TRUE(serve_config.service.interfaces.http.enabled);
  EXPECT_TRUE(serve_config.service.interfaces.grpc.enabled);
  EXPECT_EQ(4U, serve_config.service.interfaces.http.endpoint.io_threads);
  EXPECT_EQ(4U, serve_config.service.interfaces.grpc.endpoint.completion_queue_threads);
  EXPECT_EQ("../workflow_configs/service_bootstrap_rich.yaml", serve_config.service.bootstrap_request.path);

  const to::app::ApplicationConfig warehouse_config = to::app::load_application_config_from_file(
      runfiles_path("application/examples/application_configs/warehouse_continuous_pipeline.yaml").c_str());
  EXPECT_TRUE(warehouse_config.configured);
  EXPECT_EQ(to::app::ApplicationMode::Serve, warehouse_config.mode);
  EXPECT_TRUE(warehouse_config.service.control_plane.enabled);
  EXPECT_EQ(".task-orchestrator/control-plane/warehouse_continuous_pipeline.sqlite3",
            warehouse_config.service.control_plane.database_path);
  EXPECT_TRUE(warehouse_config.service.interfaces.cli.enabled);
  EXPECT_TRUE(warehouse_config.service.interfaces.http.enabled);
  EXPECT_TRUE(warehouse_config.service.interfaces.grpc.enabled);
  EXPECT_EQ(8080, warehouse_config.service.interfaces.http.endpoint.port);
  EXPECT_EQ(9090, warehouse_config.service.interfaces.grpc.endpoint.port);
  EXPECT_EQ("../workflow_configs/warehouse_continuous_pipeline.yaml", warehouse_config.service.bootstrap_request.path);
}

TEST(ConfigLoaderTest, InvalidYamlReturnsEmptyConfig) {
  const to::app::WorkflowConfig from_string = to::app::load_config_from_string("id: [");
  EXPECT_TRUE(from_string.id.empty());
  EXPECT_TRUE(from_string.actors.empty());
  EXPECT_TRUE(from_string.tasks.empty());

  const to::app::ApplicationLaunchConfig launch_from_string = to::app::load_launch_config_from_string("launch: [");
  EXPECT_FALSE(launch_from_string.configured);
  EXPECT_FALSE(launch_from_string.has_enabled_interface());

  const to::app::ApplicationConfig app_from_string = to::app::load_application_config_from_string("application: [");
  EXPECT_FALSE(app_from_string.configured);
  EXPECT_FALSE(app_from_string.valid());

  const std::filesystem::path temp_path =
      std::filesystem::temp_directory_path() / "task_orchestrator_invalid_config_loader_test.yaml";
  {
    std::ofstream output(temp_path);
    output << "id: [";
  }

  const to::app::WorkflowConfig from_file = to::app::load_config_from_file(temp_path.c_str());
  std::filesystem::remove(temp_path);

  EXPECT_TRUE(from_file.id.empty());
  EXPECT_TRUE(from_file.actors.empty());
  EXPECT_TRUE(from_file.tasks.empty());

  const to::app::ApplicationLaunchConfig launch_from_file = to::app::load_launch_config_from_file(temp_path.c_str());
  EXPECT_FALSE(launch_from_file.configured);
  EXPECT_FALSE(launch_from_file.has_enabled_interface());

  const to::app::ApplicationConfig application_from_file =
      to::app::load_application_config_from_file(temp_path.c_str());
  EXPECT_FALSE(application_from_file.configured);
  EXPECT_FALSE(application_from_file.valid());
}

TEST(ConfigLoaderTest, UnterminatedScalarsReturnEmptyConfigs) {
  const std::string invalid_workflow = "id: \"unterminated";
  const std::string invalid_launch = "launch:\n  interfaces:\n    cli:\n      prompt: \"unterminated";
  const std::string invalid_application = "application:\n  mode: \"unterminated";

  EXPECT_TRUE(to::app::load_config_from_string(invalid_workflow).id.empty());
  EXPECT_FALSE(to::app::load_launch_config_from_string(invalid_launch).configured);
  EXPECT_FALSE(to::app::load_application_config_from_string(invalid_application).configured);

  const std::filesystem::path workflow_path =
      write_temp_yaml_file("task_orchestrator_invalid_workflow_scalar.yaml", invalid_workflow);
  const std::filesystem::path launch_path =
      write_temp_yaml_file("task_orchestrator_invalid_launch_scalar.yaml", invalid_launch);
  const std::filesystem::path application_path =
      write_temp_yaml_file("task_orchestrator_invalid_application_scalar.yaml", invalid_application);

  EXPECT_TRUE(to::app::load_config_from_file(workflow_path.c_str()).id.empty());
  EXPECT_FALSE(to::app::load_launch_config_from_file(launch_path.c_str()).configured);
  EXPECT_FALSE(to::app::load_application_config_from_file(application_path.c_str()).configured);
}
}  // namespace
