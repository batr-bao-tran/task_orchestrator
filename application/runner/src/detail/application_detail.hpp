#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNNER_SRC_DETAIL__APPLICATION_DETAIL_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNNER_SRC_DETAIL__APPLICATION_DETAIL_HPP_
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "application/config/config.hpp"
#include "protocol/runtime_api.hpp"
#include "runner/run_result.hpp"
#include "runtime_service/in_memory_runtime_service.hpp"
#include "task_orchestrator/optimizer/model.hpp"
#include "utils/logger.hpp"

namespace task_orchestrator::app::detail {

std::shared_ptr<spdlog::logger> application_logger();
void install_signal_handlers();
bool workflow_has_content(const WorkflowConfig& workflow_config);
std::string trim_copy(const std::string& value);
bool read_content(std::string_view path, std::string* content);
std::filesystem::path resolve_configured_path(const std::filesystem::path& base_directory, std::string_view path);
protocol::SubmitWorkflowRequest make_submit_request(const WorkflowConfig& workflow_config,
                                                    const protocol::SecurityConfig& security_config);
protocol::pb::ClientAuthContext make_local_auth_context(const protocol::SecurityConfig& security_config);
protocol::pb::AvailabilityWindow to_proto_window(const AvailabilityWindowConfig& window_config);
protocol::pb::ActorConfig to_proto_actor(const ActorConfig& actor_config);
protocol::pb::TaskConfig to_proto_task(const TaskConfig& task_config);
protocol::pb::WorkflowConfig to_proto_workflow(const WorkflowConfig& workflow_config);
AvailabilityWindowConfig to_app_window(const AvailabilityWindow& window);
ActorConfig to_app_actor(const optimizer::OptimizerActor& actor);
TaskConfig to_app_task(const optimizer::OptimizerTask& task);
WorkflowConfig to_app_workflow(const optimizer::OptimizationModel& model);
std::string workflow_event_type_name(protocol::pb::WorkflowEventType event_type);
void log_run_result(const RunResult& result, const std::shared_ptr<spdlog::logger>& logger);
void log_runtime_response(const protocol::RuntimeApiResponse& response, const std::shared_ptr<spdlog::logger>& logger);
protocol::RuntimeApiResponse log_event_stream(protocol::WorkflowEventStream event_stream,
                                              const std::shared_ptr<spdlog::logger>& logger);

enum class CliInputState {
  Timeout,
  LineReady,
  Closed,
};

CliInputState poll_cli_input(std::string* line);
std::pair<std::string, std::string> split_command_and_argument(std::string_view command_line);
void print_cli_prompt(std::string_view prompt);
void log_cli_help(const std::shared_ptr<spdlog::logger>& logger);
std::optional<WorkflowConfig> load_request_workflow(const RequestFileConfig& request_config,
                                                    const std::filesystem::path& base_directory,
                                                    const std::shared_ptr<spdlog::logger>& logger);
bool handle_submit_yaml(const std::string& path,
                        const std::filesystem::path& base_directory,
                        InMemoryWorkflowRuntimeService& runtime_service,
                        const protocol::SecurityConfig& security_config,
                        const std::shared_ptr<spdlog::logger>& logger);
bool handle_submit_text(const std::string& path,
                        const std::filesystem::path& base_directory,
                        InMemoryWorkflowRuntimeService& runtime_service,
                        const protocol::SecurityConfig& security_config,
                        const std::shared_ptr<spdlog::logger>& logger);
bool handle_reorchestrate(const std::string& workflow_id,
                          InMemoryWorkflowRuntimeService& runtime_service,
                          const protocol::SecurityConfig& security_config,
                          const std::shared_ptr<spdlog::logger>& logger);

struct ServiceEndpoints {
  std::string http;
  std::string grpc;
};

bool handle_cli_command(const std::string& command_line,
                        const std::filesystem::path& base_directory,
                        InMemoryWorkflowRuntimeService& runtime_service,
                        const protocol::SecurityConfig& security_config,
                        const ServiceEndpoints& endpoints,
                        const std::shared_ptr<spdlog::logger>& logger);
int run_cli_loop(const std::filesystem::path& base_directory,
                 const CliInterfaceConfig& cli_config,
                 const protocol::SecurityConfig& security_config,
                 const ServiceEndpoints& endpoints,
                 InMemoryWorkflowRuntimeService& runtime_service,
                 const std::shared_ptr<spdlog::logger>& logger);
int run_one_shot_request(const RequestFileConfig& request_config,
                         const std::filesystem::path& base_directory,
                         const std::shared_ptr<spdlog::logger>& logger);
int run_service_mode(const ApplicationLaunchConfig& launch_config,
                     const std::filesystem::path& base_directory,
                     const std::shared_ptr<spdlog::logger>& logger);

}  // namespace task_orchestrator::app::detail

#endif  // TASK_ORCHESTRATOR__APPLICATION_RUNNER_SRC__APPLICATION_DETAIL_HPP_
