#include "runner/application.hpp"

#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "control_plane/service/control_plan_service.hpp"
#include "control_plane/service/event_storage_management_service.hpp"
#include "control_plane/store/sqlite_workflow_store.hpp"
#include "detail/application_detail.hpp"
#include "operator/operator_service.hpp"
#include "protocol/grpc_transport.hpp"
#include "protocol/http_transport.hpp"
#include "runner/runner.hpp"
#include "runtime_service/in_memory_runtime_service.hpp"
#include "task_orchestrator/optimizer/optimizer.hpp"
#include "utils/logger.hpp"
#include "utils/task_executor.hpp"

namespace task_orchestrator::app {

namespace detail {

namespace {

namespace pb = task_orchestrator::protocol::pb;

inline constexpr std::string_view kReadFromStdinPath = "-";
inline constexpr std::string_view kCommandHelp = "help";
inline constexpr std::string_view kCommandQuit = "quit";
inline constexpr std::string_view kCommandExit = "exit";
inline constexpr std::string_view kCommandStatus = "status";
inline constexpr std::string_view kCommandSubmitYaml = "submit-yaml";
inline constexpr std::string_view kCommandSubmitText = "submit-text";
inline constexpr std::string_view kCommandReorchestrate = "reorchestrate";
inline constexpr int kCliPollTimeoutMs = 200;
inline constexpr auto kShutdownPollInterval = std::chrono::milliseconds(kCliPollTimeoutMs);

volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_interrupt_signal(int) { g_shutdown_requested = 1; }

bool shutdown_requested() { return g_shutdown_requested != 0; }

}  // namespace

std::shared_ptr<spdlog::logger> application_logger() { return get_logger(LogLayer::Application); }

void install_signal_handlers() {
  std::signal(SIGINT, handle_interrupt_signal);
  std::signal(SIGTERM, handle_interrupt_signal);
}

bool workflow_has_content(const WorkflowConfig& workflow_config) {
  return !workflow_config.actors.empty() || !workflow_config.tasks.empty();
}

std::string trim_copy(const std::string& value) {
  const auto first_non_space = value.find_first_not_of(" \t\r\n");
  if (first_non_space == std::string::npos) {
    return {};
  }
  const auto last_non_space = value.find_last_not_of(" \t\r\n");
  return value.substr(first_non_space, last_non_space - first_non_space + 1U);
}

bool read_content(std::string_view path, std::string* content) {
  if (path == kReadFromStdinPath) {
    content->assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    return true;
  }

  std::ifstream input{std::string(path)};
  if (!input) {
    return false;
  }
  content->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return true;
}

std::filesystem::path resolve_configured_path(const std::filesystem::path& base_directory, std::string_view path) {
  std::filesystem::path configured_path(path);
  if (configured_path.is_absolute() || path == kReadFromStdinPath || base_directory.empty()) {
    return configured_path;
  }
  return base_directory / configured_path;
}

pb::ClientAuthContext make_local_auth_context(const protocol::SecurityConfig& security_config) {
  pb::ClientAuthContext auth_context;
  auth_context.set_secure_transport(true);
  if (security_config.mode == protocol::AuthMode::BearerToken) {
    auth_context.set_bearer_token(security_config.expected_credential);
  } else if (security_config.mode == protocol::AuthMode::ApiKey) {
    auth_context.set_api_key(security_config.expected_credential);
  }
  return auth_context;
}

pb::AvailabilityWindow to_proto_window(const AvailabilityWindowConfig& window_config) {
  pb::AvailabilityWindow window;
  window.set_start(window_config.start);
  window.set_end(window_config.end);
  return window;
}

pb::ActorConfig to_proto_actor(const ActorConfig& actor_config) {
  pb::ActorConfig actor;
  actor.set_id(actor_config.id);
  actor.set_type(actor_config.type);
  actor.set_capacity(actor_config.capacity);
  actor.mutable_capabilities()->Assign(actor_config.capabilities.begin(), actor_config.capabilities.end());
  actor.set_execution_cost_per_unit(actor_config.execution_cost_per_unit);
  for (const AvailabilityWindowConfig& window_config : actor_config.windows) {
    *actor.add_windows() = to_proto_window(window_config);
  }
  return actor;
}

pb::TaskConfig to_proto_task(const TaskConfig& task_config) {
  pb::TaskConfig task;
  task.set_id(task_config.id);
  task.set_requested_time(task_config.requested_time);
  task.set_duration(task_config.duration);
  task.set_latest_start_time(task_config.latest_start_time);
  task.set_deadline(task_config.deadline);
  task.set_priority(task_config.priority);
  task.set_demand(task_config.demand);
  task.set_mandatory(task_config.mandatory);
  task.set_preemptible(task_config.preemptible);
  task.mutable_allowed_actor_types()->Assign(task_config.allowed_actor_types.begin(),
                                             task_config.allowed_actor_types.end());
  task.mutable_allowed_actor_ids()->Assign(task_config.allowed_actor_ids.begin(), task_config.allowed_actor_ids.end());
  task.mutable_preferred_actor_ids()->Assign(task_config.preferred_actor_ids.begin(),
                                             task_config.preferred_actor_ids.end());
  task.mutable_required_capabilities()->Assign(task_config.required_capabilities.begin(),
                                               task_config.required_capabilities.end());
  task.mutable_dependency_task_ids()->Assign(task_config.dependency_task_ids.begin(),
                                             task_config.dependency_task_ids.end());
  task.mutable_mutually_exclusive_task_ids()->Assign(task_config.mutually_exclusive_task_ids.begin(),
                                                     task_config.mutually_exclusive_task_ids.end());
  for (const auto& [actor_id, distance] : task_config.actor_distances) {
    pb::ActorDistance* actor_distance = task.add_actor_distances();
    actor_distance->set_actor_id(actor_id);
    actor_distance->set_distance(distance);
  }
  task.set_tardiness_cost_per_unit(task_config.tardiness_cost_per_unit);
  task.set_early_start_bonus(task_config.early_start_bonus);
  task.mutable_phase_durations()->Assign(task_config.phase_durations.begin(), task_config.phase_durations.end());
  return task;
}

pb::WorkflowConfig to_proto_workflow(const WorkflowConfig& workflow_config) {
  pb::WorkflowConfig workflow;
  workflow.set_id(workflow_config.id);
  workflow.mutable_optimization()->set_backend(workflow_config.optimization.backend);
  workflow.mutable_optimization()->set_time_limit_ms(workflow_config.optimization.time_limit_ms);
  workflow.mutable_optimization()->set_relative_gap_limit(workflow_config.optimization.relative_gap_limit);
  workflow.mutable_optimization()->set_num_search_workers(workflow_config.optimization.num_search_workers);
  workflow.mutable_optimization()->set_allow_partial_plan(workflow_config.optimization.allow_partial_plan);
  workflow.mutable_optimization()->mutable_objective()->set_fulfilled_task_weight(
      workflow_config.optimization.objective.fulfilled_task_weight);
  workflow.mutable_optimization()->mutable_objective()->set_priority_weight(
      workflow_config.optimization.objective.priority_weight);
  workflow.mutable_optimization()->mutable_objective()->set_makespan_weight(
      workflow_config.optimization.objective.makespan_weight);
  workflow.mutable_optimization()->mutable_objective()->set_travel_distance_weight(
      workflow_config.optimization.objective.travel_distance_weight);
  workflow.mutable_optimization()->mutable_objective()->set_tardiness_weight(
      workflow_config.optimization.objective.tardiness_weight);
  workflow.mutable_optimization()->mutable_objective()->set_execution_cost_weight(
      workflow_config.optimization.objective.execution_cost_weight);
  workflow.mutable_optimization()->mutable_objective()->set_preferred_actor_weight(
      workflow_config.optimization.objective.preferred_actor_weight);

  for (const ActorConfig& actor_config : workflow_config.actors) {
    *workflow.add_actors() = to_proto_actor(actor_config);
  }
  for (const TaskConfig& task_config : workflow_config.tasks) {
    *workflow.add_tasks() = to_proto_task(task_config);
  }
  return workflow;
}

AvailabilityWindowConfig to_app_window(const AvailabilityWindow& window) {
  return AvailabilityWindowConfig{
      .start = window.start,
      .end = window.end,
  };
}

ActorConfig to_app_actor(const optimizer::OptimizerActor& actor) {
  ActorConfig actor_config{
      .id = actor.id,
      .type = actor.type,
      .capacity = actor.capacity,
      .windows = {},
      .capabilities = actor.capabilities,
      .execution_cost_per_unit = actor.execution_cost_per_unit,
  };
  actor_config.windows.reserve(actor.availability_windows.size());
  std::ranges::transform(actor.availability_windows, std::back_inserter(actor_config.windows), to_app_window);
  return actor_config;
}

TaskConfig to_app_task(const optimizer::OptimizerTask& task) {
  return TaskConfig{
      .id = task.id,
      .requested_time = task.release_time,
      .duration = task.duration,
      .latest_start_time = task.latest_start_time.value_or(0),
      .deadline = task.deadline.value_or(0),
      .priority = task.priority,
      .demand = task.demand,
      .mandatory = task.mandatory,
      .preemptible = task.preemptible,
      .allowed_actor_types = task.allowed_actor_types,
      .allowed_actor_ids = task.allowed_actor_ids,
      .preferred_actor_ids = task.preferred_actor_ids,
      .required_capabilities = task.required_capabilities,
      .dependency_task_ids = task.dependency_task_ids,
      .mutually_exclusive_task_ids = task.mutually_exclusive_task_ids,
      .actor_distances = task.actor_distances,
      .tardiness_cost_per_unit = task.tardiness_cost_per_unit,
      .early_start_bonus = task.early_start_bonus,
      .phase_durations = {},
  };
}

WorkflowConfig to_app_workflow(const optimizer::OptimizationModel& model) {
  WorkflowConfig workflow_config{
      .id = model.id,
      .optimization = {},
      .actors = {},
      .tasks = {},
  };
  workflow_config.actors.reserve(model.actors.size());
  workflow_config.tasks.reserve(model.tasks.size());
  std::ranges::transform(model.actors, std::back_inserter(workflow_config.actors), to_app_actor);
  std::ranges::transform(model.tasks, std::back_inserter(workflow_config.tasks), to_app_task);
  return workflow_config;
}

protocol::SubmitWorkflowRequest make_submit_request(const WorkflowConfig& workflow_config,
                                                    const protocol::SecurityConfig& security_config) {
  protocol::SubmitWorkflowRequest request;
  *request.mutable_config() = to_proto_workflow(workflow_config);
  *request.mutable_auth() = make_local_auth_context(security_config);
  request.set_replace_existing(true);
  return request;
}

std::string workflow_event_type_name(const pb::WorkflowEventType event_type) {
  switch (event_type) {
    case pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED:
      return "request_rejected";
    case pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED:
      return "workflow_accepted";
    case pb::WORKFLOW_EVENT_TYPE_RUNTIME_OVERRIDE_APPLIED:
      return "runtime_override_applied";
    case pb::WORKFLOW_EVENT_TYPE_TASK_COMPLETED:
      return "task_completed";
    case pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED:
      return "replanning_started";
    case pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED:
      return "task_planned";
    case pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED:
      return "run_finished";
    case pb::WORKFLOW_EVENT_TYPE_UNSPECIFIED:
    default:
      return "unspecified";
  }
}

void log_run_result(const RunResult& result, const std::shared_ptr<spdlog::logger>& logger) {
  if (!result.ok) {
    logger->error("Run failed: {}", result.error_message);
    return;
  }

  logger->info("Assignments: {}", result.assignments.size());
  for (const Assignment& assignment : result.assignments) {
    logger->info("{} -> {} @ {}", assignment.task_id, assignment.actor_id, assignment.start_time);
  }

  if (!result.capacity_issue) {
    return;
  }

  logger->warn("Capacity issue: {} task(s) could not be fulfilled.", result.unfulfilled_task_ids.size());
  for (const TaskId& task_id : result.unfulfilled_task_ids) {
    logger->warn("unfulfilled: {}", task_id);
  }
}

void log_runtime_response(const protocol::RuntimeApiResponse& response, const std::shared_ptr<spdlog::logger>& logger) {
  if (!response.ok()) {
    logger->error("Runtime request failed: {}", response.error_message());
    return;
  }

  logger->info("Runtime request completed with {} assignment(s).", response.result().assignments_size());
  if (response.result().capacity_issue()) {
    logger->warn("Runtime request left {} task(s) unfulfilled.", response.result().unfulfilled_task_ids_size());
  }
}

protocol::RuntimeApiResponse log_event_stream(protocol::WorkflowEventStream event_stream,
                                              const std::shared_ptr<spdlog::logger>& logger) {
  protocol::RuntimeApiResponse final_response;
  for (const protocol::WorkflowEvent& event : event_stream) {
    logger->info(
        "workflow={} event={} detail={}", event.workflow_id(), workflow_event_type_name(event.type()), event.detail());
    if (!event.task_id().empty()) {
      logger->info(
          "task={} actor={} start={} end={}", event.task_id(), event.actor_id(), event.start_time(), event.end_time());
    }
    if (event.has_response()) {
      final_response = event.response();
    }
  }
  return final_response;
}

CliInputState poll_cli_input(std::string* line) {
  pollfd stdin_descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
  const int poll_result = ::poll(&stdin_descriptor, 1, kCliPollTimeoutMs);
  if (poll_result <= 0) {
    return CliInputState::Timeout;
  }
  if ((stdin_descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    return CliInputState::Closed;
  }
  if ((stdin_descriptor.revents & POLLIN) == 0) {
    return CliInputState::Timeout;
  }
  if (!std::getline(std::cin, *line)) {
    return CliInputState::Closed;
  }
  return CliInputState::LineReady;
}

std::pair<std::string, std::string> split_command_and_argument(std::string_view command_line) {
  const std::string trimmed_line = trim_copy(std::string(command_line));
  const auto separator = trimmed_line.find_first_of(" \t");
  if (separator == std::string::npos) {
    return {trimmed_line, {}};
  }
  return {trimmed_line.substr(0, separator), trim_copy(trimmed_line.substr(separator + 1U))};
}

void print_cli_prompt(std::string_view prompt) {
  std::fwrite(prompt.data(), sizeof(char), prompt.size(), stdout);
  std::fflush(stdout);
}

void log_cli_help(const std::shared_ptr<spdlog::logger>& logger) {
  logger->info("CLI commands:");
  logger->info("  help");
  logger->info("  status");
  logger->info("  submit-yaml <path>");
  logger->info("  submit-text <path>");
  logger->info("  reorchestrate <workflow_id>");
  logger->info("  quit");
}

std::optional<WorkflowConfig> load_request_workflow(const RequestFileConfig& request_config,
                                                    const std::filesystem::path& base_directory,
                                                    const std::shared_ptr<spdlog::logger>& logger) {
  if (!request_config.configured()) {
    logger->error("Request config is missing a supported file kind or path.");
    return std::nullopt;
  }

  const std::filesystem::path resolved_path = resolve_configured_path(base_directory, request_config.path);
  if (request_config.kind == RequestFileKind::WorkflowYaml) {
    WorkflowConfig workflow_config = load_config_from_file(resolved_path.c_str());
    if (!workflow_has_content(workflow_config)) {
      logger->error("No workflow could be loaded from '{}'.", resolved_path.string());
      return std::nullopt;
    }
    return workflow_config;
  }

  std::string content;
  if (!read_content(resolved_path.string(), &content)) {
    logger->error("Failed to read workflow text from '{}'.", resolved_path.string());
    return std::nullopt;
  }

  const optimizer::ParseResult parsed_request = task_orchestrator::optimizer::WorkflowOptimizer::parse_text(content);
  if (!parsed_request.ok) {
    logger->error("Failed to parse workflow text '{}': {}", resolved_path.string(), parsed_request.error_message);
    return std::nullopt;
  }
  return to_app_workflow(parsed_request.model);
}

bool handle_submit_yaml(const std::string& path,
                        const std::filesystem::path& base_directory,
                        protocol::WorkflowRuntimeService& runtime_service,
                        const protocol::SecurityConfig& security_config,
                        const std::shared_ptr<spdlog::logger>& logger) {
  if (path == kReadFromStdinPath) {
    logger->error("submit-yaml does not support '-' while the interactive CLI owns stdin.");
    return true;
  }

  const auto workflow_config = load_request_workflow(
      RequestFileConfig{.kind = RequestFileKind::WorkflowYaml, .path = path}, base_directory, logger);
  if (!workflow_config.has_value()) {
    return true;
  }

  log_runtime_response(
      log_event_stream(runtime_service.stream_submit_workflow(make_submit_request(*workflow_config, security_config)),
                       logger),
      logger);
  return true;
}

bool handle_submit_text(const std::string& path,
                        const std::filesystem::path& base_directory,
                        protocol::WorkflowRuntimeService& runtime_service,
                        const protocol::SecurityConfig& security_config,
                        const std::shared_ptr<spdlog::logger>& logger) {
  if (path == kReadFromStdinPath) {
    logger->error("submit-text does not support '-' while the interactive CLI owns stdin.");
    return true;
  }

  const auto workflow_config = load_request_workflow(
      RequestFileConfig{.kind = RequestFileKind::WorkflowText, .path = path}, base_directory, logger);
  if (!workflow_config.has_value()) {
    return true;
  }

  log_runtime_response(
      log_event_stream(runtime_service.stream_submit_workflow(make_submit_request(*workflow_config, security_config)),
                       logger),
      logger);
  return true;
}

bool handle_reorchestrate(const std::string& workflow_id,
                          protocol::WorkflowRuntimeService& runtime_service,
                          const protocol::SecurityConfig& security_config,
                          const std::shared_ptr<spdlog::logger>& logger) {
  if (workflow_id.empty()) {
    logger->error("Usage: reorchestrate <workflow_id>");
    return true;
  }

  protocol::ReorchestrateRequest request;
  request.set_workflow_id(workflow_id);
  *request.mutable_auth() = make_local_auth_context(security_config);
  request.set_trigger_reorchestration(true);
  log_runtime_response(log_event_stream(runtime_service.stream_reorchestrate(std::move(request)), logger), logger);
  return true;
}

bool handle_cli_command(const std::string& command_line,
                        const std::filesystem::path& base_directory,
                        protocol::WorkflowRuntimeService& runtime_service,
                        const protocol::SecurityConfig& security_config,
                        const ServiceEndpoints& endpoints,
                        const std::shared_ptr<spdlog::logger>& logger) {
  const auto [command, argument] = split_command_and_argument(command_line);
  if (command.empty()) {
    return true;
  }
  if (command == kCommandHelp) {
    log_cli_help(logger);
    return true;
  }
  if (command == kCommandStatus) {
    logger->info("HTTP endpoint: {}", endpoints.http.empty() ? "disabled" : endpoints.http);
    logger->info("gRPC endpoint: {}", endpoints.grpc.empty() ? "disabled" : endpoints.grpc);
    return true;
  }
  if (command == kCommandSubmitYaml) {
    if (argument.empty()) {
      logger->error("Usage: submit-yaml <path>");
      return true;
    }
    return handle_submit_yaml(argument, base_directory, runtime_service, security_config, logger);
  }
  if (command == kCommandSubmitText) {
    if (argument.empty()) {
      logger->error("Usage: submit-text <path>");
      return true;
    }
    return handle_submit_text(argument, base_directory, runtime_service, security_config, logger);
  }
  if (command == kCommandReorchestrate) {
    return handle_reorchestrate(argument, runtime_service, security_config, logger);
  }
  if (command == kCommandQuit || command == kCommandExit) {
    logger->info("Shutdown requested from CLI.");
    return false;
  }

  logger->warn("Unknown CLI command '{}'. Type 'help' for available commands.", command);
  return true;
}

int run_cli_loop(const std::filesystem::path& base_directory,
                 const CliInterfaceConfig& cli_config,
                 const protocol::SecurityConfig& security_config,
                 const ServiceEndpoints& endpoints,
                 protocol::WorkflowRuntimeService& runtime_service,
                 const std::shared_ptr<spdlog::logger>& logger) {
  logger->info("Interactive CLI is ready. Type 'help' for commands.");

  bool should_print_prompt = true;
  while (!shutdown_requested()) {
    if (should_print_prompt) {
      print_cli_prompt(cli_config.prompt);
      should_print_prompt = false;
    }

    std::string command_line;
    switch (poll_cli_input(&command_line)) {
      case CliInputState::Timeout:
        continue;
      case CliInputState::Closed:
        logger->info("CLI input closed.");
        return EXIT_SUCCESS;
      case CliInputState::LineReady:
        should_print_prompt = true;
        if (!handle_cli_command(command_line, base_directory, runtime_service, security_config, endpoints, logger)) {
          return EXIT_SUCCESS;
        }
        continue;
    }
  }

  logger->info("CLI stopping after interrupt signal.");
  return EXIT_SUCCESS;
}

int run_one_shot_request(const RequestFileConfig& request_config,
                         const std::filesystem::path& base_directory,
                         const std::shared_ptr<spdlog::logger>& logger) {
  if (!request_config.configured()) {
    logger->error("One-shot mode requires `application.request.kind` and `application.request.path`.");
    return EXIT_FAILURE;
  }

  if (request_config.kind == RequestFileKind::WorkflowYaml) {
    const auto workflow_config = load_request_workflow(request_config, base_directory, logger);
    if (!workflow_config.has_value()) {
      return EXIT_FAILURE;
    }

    const RunResult result = run(*workflow_config);
    log_run_result(result, logger);
    return (!result.ok || result.capacity_issue) ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  const std::filesystem::path resolved_path = resolve_configured_path(base_directory, request_config.path);
  std::string content;
  if (!read_content(resolved_path.string(), &content)) {
    logger->error("Failed to read workflow text from '{}'.", resolved_path.string());
    return EXIT_FAILURE;
  }

  const RunResult result = optimize_text(content);
  log_run_result(result, logger);
  return (!result.ok || result.capacity_issue) ? EXIT_FAILURE : EXIT_SUCCESS;
}

int run_service_mode(const ApplicationLaunchConfig& launch_config,
                     const std::filesystem::path& base_directory,
                     const std::shared_ptr<spdlog::logger>& logger) {
  if (!launch_config.has_enabled_interface()) {
    logger->error("Service mode requires at least one enabled interface.");
    return EXIT_FAILURE;
  }

  struct RuntimeServiceBundle {
    std::unique_ptr<protocol::WorkflowRuntimeService> runtime_service;
    control_plane::service::ControlPlanService* control_plan_service = nullptr;
    std::shared_ptr<control_plane::store::WorkflowStore> workflow_store;
    std::shared_ptr<control_plane::integration::ConnectorRegistry> connector_registry;
  };

  RuntimeServiceBundle service_bundle;
  std::unique_ptr<app::operator_api::OperatorService> operator_service;
  std::shared_ptr<control_plane::service::WorkflowUpdateFeed> workflow_update_feed;
  if (launch_config.control_plane.configured()) {
    auto workflow_store =
        std::make_shared<control_plane::store::SqliteWorkflowStore>(launch_config.control_plane.database_path);
    auto connector_registry = control_plane::integration::make_in_memory_connector_registry();
    workflow_update_feed = control_plane::service::make_in_memory_workflow_update_feed();
    control_plane::service::EventStorageManagementService event_storage_management(
        workflow_store, launch_config.control_plane.prune_after_days);
    if (launch_config.control_plane.pruning_enabled()) {
      const auto prune_outcome = event_storage_management.prune_stale_boot_history();
      logger->info(
          "Control-plane storage prune: performed={}, cutoff_unix_ms={}, events={}, plans={}, audits={}, "
          "idempotency={}.",
          prune_outcome.performed,
          prune_outcome.cutoff_unix_ms,
          prune_outcome.result.pruned_event_rows,
          prune_outcome.result.pruned_plan_version_rows,
          prune_outcome.result.pruned_audit_entry_rows,
          prune_outcome.result.pruned_idempotency_rows);
    }

    auto control_plan_runtime_service = std::make_unique<control_plane::service::ControlPlanService>(
        std::make_unique<InMemoryWorkflowRuntimeService>(launch_config.security),
        workflow_store,
        workflow_update_feed,
        connector_registry);
    service_bundle.control_plan_service = control_plan_runtime_service.get();
    service_bundle.workflow_store = workflow_store;
    service_bundle.connector_registry = connector_registry;
    service_bundle.runtime_service = std::move(control_plan_runtime_service);
    operator_service = std::make_unique<app::operator_api::OperatorService>(*service_bundle.runtime_service,
                                                                            *service_bundle.control_plan_service,
                                                                            workflow_store,
                                                                            connector_registry,
                                                                            workflow_update_feed);
  } else {
    service_bundle.runtime_service = std::make_unique<InMemoryWorkflowRuntimeService>(launch_config.security);
  }
  protocol::WorkflowRuntimeService& runtime_service = *service_bundle.runtime_service;

  if (service_bundle.control_plan_service != nullptr && launch_config.control_plane.recover_on_start) {
    service_bundle.control_plan_service->recover_active_workflows(make_local_auth_context(launch_config.security));
  }

  if (launch_config.bootstrap_request.configured()) {
    const auto bootstrap_workflow = load_request_workflow(launch_config.bootstrap_request, base_directory, logger);
    if (!bootstrap_workflow.has_value()) {
      return EXIT_FAILURE;
    }
    const protocol::RuntimeApiResponse response =
        runtime_service.submit_workflow(make_submit_request(*bootstrap_workflow, launch_config.security));
    log_runtime_response(response, logger);
  } else if (launch_config.bootstrap_workflow.has_value()) {
    const protocol::RuntimeApiResponse response =
        runtime_service.submit_workflow(make_submit_request(*launch_config.bootstrap_workflow, launch_config.security));
    log_runtime_response(response, logger);
  }

  ServiceEndpoints endpoints;
  std::unique_ptr<protocol::HttpWorkflowApiServer> http_server;
  std::unique_ptr<protocol::AsyncGrpcWorkflowApiServer> grpc_server;

  if (launch_config.interfaces.http.enabled) {
    http_server =
        std::make_unique<protocol::BeastHttpWorkflowApiServer>(runtime_service,
                                                               launch_config.interfaces.http.endpoint,
                                                               protocol::make_default_tls_credential_provider(),
                                                               operator_service.get(),
                                                               operator_service.get());
    http_server->start();
    if (!http_server->running()) {
      logger->error("HTTP server failed to start.");
      return EXIT_FAILURE;
    }
    endpoints.http = http_server->endpoint();
  }

  if (launch_config.interfaces.grpc.enabled) {
    grpc_server =
        std::make_unique<protocol::GrpcWorkflowApiServer>(runtime_service, launch_config.interfaces.grpc.endpoint);
    grpc_server->start();
    if (!grpc_server->running()) {
      logger->error("gRPC server failed to start.");
      if (http_server) {
        http_server->stop();
      }
      return EXIT_FAILURE;
    }
    endpoints.grpc = grpc_server->endpoint();
  }

  int exit_code = EXIT_SUCCESS;
  if (launch_config.interfaces.cli.enabled) {
    exit_code = run_cli_loop(
        base_directory, launch_config.interfaces.cli, launch_config.security, endpoints, runtime_service, logger);
  } else {
    logger->info("Runtime service is running. Press Ctrl+C to stop.");
    while (!shutdown_requested()) {
      std::this_thread::sleep_for(kShutdownPollInterval);
    }
    logger->info("Interrupt received. Stopping runtime service.");
  }

  if (workflow_update_feed) {
    workflow_update_feed->shutdown();
  }
  if (grpc_server) {
    grpc_server->stop();
  }
  if (http_server) {
    http_server->stop();
  }
  get_shared_task_executor().stop();
  return exit_code;
}

}  // namespace detail

int Application::run_from_args(int argc, char** argv) {
  detail::install_signal_handlers();
  const auto logger = detail::application_logger();

  const char* path = argc >= 2 ? argv[1] : nullptr;
  if (!path) {
    logger->error("Usage: {} <application_config>", argc > 0 ? argv[0] : "run_config");
    logger->error("application_config: path to a structured application YAML file or `-` for stdin.");
    return EXIT_FAILURE;
  }
  return run_from_file(path);
}

int Application::run_from_file(const char* config_path) {
  const auto logger = detail::application_logger();
  std::string content;
  if (!detail::read_content(config_path, &content)) {
    logger->error("Failed to open application config '{}'.", config_path);
    return EXIT_FAILURE;
  }

  const ApplicationConfig config = load_application_config_from_string(content);
  if (!config.configured) {
    logger->error("Application config must define an `application:` section with a supported mode.");
    return EXIT_FAILURE;
  }

  const std::filesystem::path config_directory = std::string_view(config_path) == detail::kReadFromStdinPath
                                                     ? std::filesystem::current_path()
                                                     : std::filesystem::absolute(config_path).parent_path();
  return run(config, config_directory);
}

int Application::run(const ApplicationConfig& config, const std::filesystem::path& config_directory) {
  const auto logger = detail::application_logger();
  if (!config.valid()) {
    logger->error("Application config is incomplete for the selected mode.");
    return EXIT_FAILURE;
  }

  return config.mode == ApplicationMode::Serve ? detail::run_service_mode(config.service, config_directory, logger)
                                               : detail::run_one_shot_request(config.request, config_directory, logger);
}

}  // namespace task_orchestrator::app
