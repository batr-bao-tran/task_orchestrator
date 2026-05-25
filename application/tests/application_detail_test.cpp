#include "runner/src/detail/application_detail.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "runtime_service/in_memory_runtime_service.hpp"
#include "runtime_service/src/in_memory_runtime_service_detail.hpp"
#include "task_orchestrator/optimizer/model.hpp"

namespace {
namespace to = task_orchestrator;

class ScopedTempDirectory final {
 public:
  ScopedTempDirectory() {
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::filesystem::file_time_type::clock::now().time_since_epoch())
                               .count();
    const auto unique_suffix = std::to_string(::getpid()) + "_" + std::to_string(static_cast<long long>(timestamp));
    path_ = std::filesystem::temp_directory_path() / ("task_orchestrator_application_detail_test_" + unique_suffix);
    std::filesystem::create_directories(path_);
  }

  ~ScopedTempDirectory() noexcept {
    std::error_code error_code;
    std::filesystem::remove_all(path_, error_code);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

class ScopedStdinPipe final {
 public:
  ScopedStdinPipe(std::string_view content, bool close_writer = true) {
    std::array<int, 2> pipe_descriptors = {-1, -1};
    if (::pipe(pipe_descriptors.data()) != 0) {
      return;
    }

    original_stdin_ = ::dup(STDIN_FILENO);
    if (original_stdin_ < 0) {
      close_pipe_descriptors(pipe_descriptors[0], pipe_descriptors[1]);
      return;
    }

    if (::dup2(pipe_descriptors[0], STDIN_FILENO) < 0) {
      close_pipe_descriptors(pipe_descriptors[0], pipe_descriptors[1]);
      close_original_stdin();
      return;
    }

    std::cin.clear();
    read_descriptor_ = pipe_descriptors[0];
    write_descriptor_ = pipe_descriptors[1];

    const auto bytes_written = ::write(write_descriptor_, content.data(), content.size());
    if (bytes_written < 0 || static_cast<std::size_t>(bytes_written) != content.size()) {
      restore_stdin();
      close_pipe_descriptors(read_descriptor_, write_descriptor_);
      read_descriptor_ = -1;
      write_descriptor_ = -1;
      return;
    }

    if (close_writer) {
      ::close(write_descriptor_);
      write_descriptor_ = -1;
    }
    active_ = true;
  }

  ~ScopedStdinPipe() noexcept {
    if (read_descriptor_ >= 0) {
      ::close(read_descriptor_);
    }
    if (write_descriptor_ >= 0) {
      ::close(write_descriptor_);
    }
    restore_stdin();
    std::cin.clear();
  }

  [[nodiscard]] bool active() const noexcept { return active_; }

 private:
  static void close_pipe_descriptors(const int read_descriptor, const int write_descriptor) noexcept {
    if (read_descriptor >= 0) {
      ::close(read_descriptor);
    }
    if (write_descriptor >= 0) {
      ::close(write_descriptor);
    }
  }

  void close_original_stdin() noexcept {
    if (original_stdin_ >= 0) {
      ::close(original_stdin_);
      original_stdin_ = -1;
    }
  }

  void restore_stdin() noexcept {
    if (original_stdin_ >= 0) {
      ::dup2(original_stdin_, STDIN_FILENO);
      ::close(original_stdin_);
      original_stdin_ = -1;
    }
  }

  int original_stdin_ = -1;
  int read_descriptor_ = -1;
  int write_descriptor_ = -1;
  bool active_ = false;
};

std::filesystem::path write_file(const std::filesystem::path& path, std::string_view content) {
  std::ofstream output(path, std::ios::binary);
  output << content;
  output.close();
  return path;
}

std::string workflow_yaml(std::string_view workflow_id) {
  return "id: " + std::string(workflow_id) + R"(
optimization:
  backend: indexed_exact
actors:
  - id: robot_1
    type: robot
    capacity: 1
    windows:
      - start: 0
        end: 100
tasks:
  - id: pick
    requested_time: 0
    duration: 5
    deadline: 20
    priority: 10
    allowed_actor_types: [robot]
  - id: pack
    requested_time: 5
    duration: 5
    deadline: 30
    priority: 5
    allowed_actor_types: [robot]
    dependency_task_ids: [pick]
)";
}

std::string workflow_text(std::string_view workflow_id) {
  return "workflow " + std::string(workflow_id) + R"(
actors:
- actor robot_1 type robot capacity 1 windows 0-100
tasks:
- task pick duration 5 release 0 deadline 20 priority 10 requires_type robot preferred_actor robot_1
- task pack duration 5 release 5 deadline 30 priority 5 requires_type robot depends_on pick
)";
}

to::app::WorkflowConfig rich_workflow_config(std::string workflow_id) {
  return to::app::WorkflowConfig{
      .id = std::move(workflow_id),
      .optimization =
          {
              .backend = "indexed_exact",
              .time_limit_ms = 250,
              .relative_gap_limit = 0.1,
              .num_search_workers = 2,
              .allow_partial_plan = false,
              .objective =
                  {
                      .fulfilled_task_weight = 10,
                      .priority_weight = 8,
                      .makespan_weight = 6,
                      .travel_distance_weight = 4,
                      .tardiness_weight = 3,
                      .execution_cost_weight = 2,
                      .preferred_actor_weight = 1,
                  },
          },
      .actors = {{
          .id = "robot_1",
          .type = "robot",
          .capacity = 1,
          .windows =
              {
                  {.start = 0, .end = 30},
                  {.start = 40, .end = 100},
              },
          .capabilities = {"pick", "pack"},
          .execution_cost_per_unit = 1.5,
      }},
      .tasks = {{
                    .id = "pick",
                    .requested_time = 0,
                    .duration = 5,
                    .latest_start_time = 3,
                    .deadline = 20,
                    .priority = 10,
                    .demand = 1,
                    .mandatory = true,
                    .preemptible = false,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {"robot_1"},
                    .preferred_actor_ids = {"robot_1"},
                    .required_capabilities = {"pick"},
                    .dependency_task_ids = {},
                    .mutually_exclusive_task_ids = {},
                    .actor_distances = {{"robot_1", 2}},
                    .tardiness_cost_per_unit = 1.2,
                    .early_start_bonus = 0.5,
                    .phase_durations = {2, 3},
                },
                {
                    .id = "pack",
                    .requested_time = 5,
                    .duration = 4,
                    .latest_start_time = 6,
                    .deadline = 25,
                    .priority = 7,
                    .demand = 1,
                    .mandatory = true,
                    .preemptible = true,
                    .allowed_actor_types = {"robot"},
                    .allowed_actor_ids = {"robot_1"},
                    .preferred_actor_ids = {"robot_1"},
                    .required_capabilities = {"pack"},
                    .dependency_task_ids = {"pick"},
                    .mutually_exclusive_task_ids = {},
                    .actor_distances = {{"robot_1", 4}},
                    .tardiness_cost_per_unit = 0.8,
                    .early_start_bonus = 0.2,
                    .phase_durations = {1, 3},
                }},
  };
}

to::optimizer::OptimizationModel rich_optimization_model() {
  return to::optimizer::OptimizationModel{
      .id = "optimized",
      .actors = {{
          .id = "robot_1",
          .type = "robot",
          .capacity = 1,
          .availability_windows =
              {
                  {.start = 0, .end = 20},
                  {.start = 30, .end = 60},
              },
          .capabilities = {"pick"},
          .execution_cost_per_unit = 1.5,
      }},
      .tasks = {{
          .id = "pick",
          .duration = 5,
          .release_time = 0,
          .latest_start_time = 4,
          .deadline = 20,
          .priority = 9,
          .demand = 1,
          .mandatory = true,
          .preemptible = false,
          .allowed_actor_types = {"robot"},
          .allowed_actor_ids = {"robot_1"},
          .preferred_actor_ids = {"robot_1"},
          .required_capabilities = {"pick"},
          .dependency_task_ids = {},
          .mutually_exclusive_task_ids = {},
          .actor_distances = {{"robot_1", 3}},
          .tardiness_cost_per_unit = 1.1,
          .early_start_bonus = 0.4,
      }},
  };
}

to::protocol::WorkflowEventStream make_event_stream() {
  using EventType = to::protocol::pb::WorkflowEventType;

  to::protocol::WorkflowEvent accepted;
  accepted.set_workflow_id("workflow_a");
  accepted.set_type(EventType::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED);
  accepted.set_detail("accepted");
  co_yield accepted;

  to::protocol::WorkflowEvent planned;
  planned.set_workflow_id("workflow_a");
  planned.set_type(EventType::WORKFLOW_EVENT_TYPE_TASK_PLANNED);
  planned.set_task_id("pick");
  planned.set_actor_id("robot_1");
  planned.set_start_time(0);
  planned.set_end_time(5);
  planned.set_detail("planned");
  co_yield planned;

  to::protocol::WorkflowEvent finished;
  finished.set_workflow_id("workflow_a");
  finished.set_type(EventType::WORKFLOW_EVENT_TYPE_RUN_FINISHED);
  finished.set_detail("done");
  finished.mutable_response()->set_ok(true);
  finished.mutable_response()->mutable_result()->set_ok(true);
  co_yield finished;
}

to::protocol::WorkflowEventStream make_event_stream_without_response() {
  to::protocol::WorkflowEvent accepted;
  accepted.set_workflow_id("workflow_b");
  accepted.set_type(to::protocol::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED);
  accepted.set_detail("accepted");
  co_yield accepted;
}

to::protocol::WorkflowEventStream make_runtime_service_response_stream(
    const to::protocol::RuntimeApiResponse& response) {
  to::protocol::WorkflowEvent accepted;
  accepted.set_workflow_id("runtime_detail");
  accepted.set_type(to::protocol::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED);
  accepted.set_detail("accepted");
  co_yield accepted;

  to::protocol::WorkflowEvent finished;
  finished.set_workflow_id("runtime_detail");
  finished.set_type(to::protocol::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED);
  finished.set_detail("done");
  *finished.mutable_response() = response;
  co_yield finished;
}

TEST(ApplicationDetailTest, ContentAndPathHelpersHandleWhitespaceFilesAndStdin) {
  const ScopedTempDirectory temp_directory;
  const std::filesystem::path file_path = write_file(temp_directory.path() / "workflow.txt", "hello world");

  EXPECT_EQ("trimmed value", to::app::detail::trim_copy(" \ttrimmed value \n"));
  EXPECT_TRUE(to::app::detail::trim_copy("   \r\n").empty());

  std::string file_content;
  ASSERT_TRUE(to::app::detail::read_content(file_path.string(), &file_content));
  EXPECT_EQ("hello world", file_content);

  const ScopedStdinPipe stdin_pipe("stdin text");
  ASSERT_TRUE(stdin_pipe.active());
  std::string stdin_content;
  ASSERT_TRUE(to::app::detail::read_content("-", &stdin_content));
  EXPECT_EQ("stdin text", stdin_content);

  EXPECT_FALSE(to::app::detail::read_content((temp_directory.path() / "missing.txt").string(), &file_content));

  EXPECT_EQ(temp_directory.path() / "relative.txt",
            to::app::detail::resolve_configured_path(temp_directory.path(), "relative.txt"));
  EXPECT_EQ(std::filesystem::path("/tmp/absolute.txt"),
            to::app::detail::resolve_configured_path(temp_directory.path(), "/tmp/absolute.txt"));
  EXPECT_EQ(std::filesystem::path("-"), to::app::detail::resolve_configured_path(temp_directory.path(), "-"));
}

TEST(ApplicationDetailTest, AuthAndWorkflowConversionHelpersPreserveConfiguredFields) {
  const auto workflow = rich_workflow_config("rich_workflow");

  const auto no_auth = to::app::detail::make_local_auth_context({});
  EXPECT_TRUE(no_auth.secure_transport());
  EXPECT_TRUE(no_auth.bearer_token().empty());
  EXPECT_TRUE(no_auth.api_key().empty());

  const auto bearer_auth = to::app::detail::make_local_auth_context(
      {.mode = to::protocol::AuthMode::BearerToken, .expected_credential = "bearer"});
  EXPECT_EQ("bearer", bearer_auth.bearer_token());

  const auto api_key_auth = to::app::detail::make_local_auth_context(
      {.mode = to::protocol::AuthMode::ApiKey, .expected_credential = "api-key"});
  EXPECT_EQ("api-key", api_key_auth.api_key());

  const auto proto_window = to::app::detail::to_proto_window(workflow.actors.front().windows.front());
  EXPECT_EQ(0, proto_window.start());
  EXPECT_EQ(30, proto_window.end());

  const auto proto_actor = to::app::detail::to_proto_actor(workflow.actors.front());
  EXPECT_EQ("robot_1", proto_actor.id());
  EXPECT_EQ(2, proto_actor.windows_size());
  EXPECT_EQ(2, proto_actor.capabilities_size());

  const auto proto_task = to::app::detail::to_proto_task(workflow.tasks.front());
  EXPECT_EQ("pick", proto_task.id());
  EXPECT_EQ(1, proto_task.actor_distances_size());
  EXPECT_EQ(2, proto_task.phase_durations_size());

  const auto proto_workflow = to::app::detail::to_proto_workflow(workflow);
  EXPECT_EQ("rich_workflow", proto_workflow.id());
  EXPECT_EQ(1, proto_workflow.actors_size());
  EXPECT_EQ(2, proto_workflow.tasks_size());
  EXPECT_FALSE(proto_workflow.optimization().allow_partial_plan());

  const auto submit_request = to::app::detail::make_submit_request(
      workflow, {.mode = to::protocol::AuthMode::ApiKey, .expected_credential = "k"});
  EXPECT_TRUE(submit_request.replace_existing());
  EXPECT_EQ("rich_workflow", submit_request.config().id());
  EXPECT_EQ("k", submit_request.auth().api_key());

  const auto model = rich_optimization_model();
  const auto app_window = to::app::detail::to_app_window(model.actors.front().availability_windows.front());
  EXPECT_EQ(0, app_window.start);
  EXPECT_EQ(20, app_window.end);

  const auto app_actor = to::app::detail::to_app_actor(model.actors.front());
  EXPECT_EQ("robot_1", app_actor.id);
  EXPECT_EQ(2U, app_actor.windows.size());
  EXPECT_EQ(1U, app_actor.capabilities.size());

  const auto app_task = to::app::detail::to_app_task(model.tasks.front());
  EXPECT_EQ("pick", app_task.id);
  EXPECT_EQ(4, app_task.latest_start_time);
  EXPECT_EQ(20, app_task.deadline);
  EXPECT_EQ("robot_1", app_task.allowed_actor_ids.front());

  const auto app_workflow = to::app::detail::to_app_workflow(model);
  EXPECT_EQ("optimized", app_workflow.id);
  EXPECT_EQ(1U, app_workflow.actors.size());
  EXPECT_EQ(1U, app_workflow.tasks.size());
}

TEST(ApplicationDetailTest, EventAndLoggingHelpersHandleAllKnownWorkflowEventTypes) {
  using EventType = to::protocol::pb::WorkflowEventType;

  EXPECT_EQ("request_rejected",
            to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED));
  EXPECT_EQ("workflow_accepted",
            to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED));
  EXPECT_EQ("runtime_override_applied",
            to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_RUNTIME_OVERRIDE_APPLIED));
  EXPECT_EQ("task_completed", to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_TASK_COMPLETED));
  EXPECT_EQ("replanning_started",
            to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED));
  EXPECT_EQ("task_planned", to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_TASK_PLANNED));
  EXPECT_EQ("run_finished", to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_RUN_FINISHED));
  EXPECT_EQ("unspecified", to::app::detail::workflow_event_type_name(EventType::WORKFLOW_EVENT_TYPE_UNSPECIFIED));

  const auto logger = to::app::detail::application_logger();
  to::app::detail::log_run_result(
      {
          .ok = false,
          .capacity_issue = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .error_message = "failed",
      },
      logger);
  to::app::detail::log_run_result(
      {
          .ok = true,
          .capacity_issue = true,
          .assignments = {{
              .task_id = "pick",
              .actor_id = "robot_1",
              .start_time = 0,
          }},
          .unfulfilled_task_ids = {"pack"},
          .error_message = {},
      },
      logger);

  to::protocol::RuntimeApiResponse response;
  response.set_ok(false);
  response.set_error_message("bad");
  to::app::detail::log_runtime_response(response, logger);
  response.set_ok(true);
  response.mutable_result()->set_ok(true);
  response.mutable_result()->add_assignments()->set_task_id("pick");
  response.mutable_result()->set_capacity_issue(true);
  response.mutable_result()->add_unfulfilled_task_ids("pack");
  to::app::detail::log_runtime_response(response, logger);

  const auto final_response = to::app::detail::log_event_stream(make_event_stream(), logger);
  EXPECT_TRUE(final_response.ok());

  const auto incomplete_response = to::app::detail::log_event_stream(make_event_stream_without_response(), logger);
  EXPECT_FALSE(incomplete_response.ok());
}

TEST(ApplicationDetailTest, CliParsingHelpersHandleTimeoutClosedReadyAndFormatting) {
  const auto logger = to::app::detail::application_logger();
  to::app::detail::log_cli_help(logger);

  {
    const ScopedStdinPipe timeout_pipe("", false);
    ASSERT_TRUE(timeout_pipe.active());
    std::string line;
    EXPECT_EQ(to::app::detail::CliInputState::Timeout, to::app::detail::poll_cli_input(&line));
  }

  {
    const ScopedStdinPipe closed_pipe("");
    ASSERT_TRUE(closed_pipe.active());
    std::string line;
    EXPECT_EQ(to::app::detail::CliInputState::Closed, to::app::detail::poll_cli_input(&line));
  }

  {
    const ScopedStdinPipe ready_pipe(" help  \n", false);
    ASSERT_TRUE(ready_pipe.active());
    std::string line;
    const auto input_state = to::app::detail::poll_cli_input(&line);
    EXPECT_NE(to::app::detail::CliInputState::Timeout, input_state);
  }

  EXPECT_EQ(std::make_pair(std::string("status"), std::string("")),
            to::app::detail::split_command_and_argument("status"));
  EXPECT_EQ(std::make_pair(std::string("submit-yaml"), std::string("file.yaml")),
            to::app::detail::split_command_and_argument("  submit-yaml   file.yaml  "));
  EXPECT_EQ(std::make_pair(std::string(), std::string()), to::app::detail::split_command_and_argument(" \t \n "));

  to::app::detail::print_cli_prompt("detail> ");
}

TEST(ApplicationDetailTest, WorkflowLoadingAndCommandHelpersHandleErrorsAndSuccesses) {
  const ScopedTempDirectory temp_directory;
  const auto logger = to::app::detail::application_logger();
  write_file(temp_directory.path() / "workflow.yaml", workflow_yaml("detail_yaml"));
  write_file(temp_directory.path() / "workflow.txt", workflow_text("detail_text"));
  write_file(temp_directory.path() / "invalid.txt", "not valid controlled text");

  to::app::InMemoryWorkflowRuntimeService runtime_service(to::protocol::SecurityConfig{});

  EXPECT_FALSE(to::app::detail::load_request_workflow({}, temp_directory.path(), logger).has_value());
  EXPECT_FALSE(
      to::app::detail::load_request_workflow(
          {.kind = to::app::RequestFileKind::WorkflowText, .path = "missing.txt"}, temp_directory.path(), logger)
          .has_value());
  EXPECT_FALSE(
      to::app::detail::load_request_workflow(
          {.kind = to::app::RequestFileKind::WorkflowText, .path = "invalid.txt"}, temp_directory.path(), logger)
          .has_value());

  const auto yaml_workflow = to::app::detail::load_request_workflow(
      {.kind = to::app::RequestFileKind::WorkflowYaml, .path = "workflow.yaml"}, temp_directory.path(), logger);
  ASSERT_TRUE(yaml_workflow.has_value());
  EXPECT_EQ("detail_yaml", yaml_workflow->id);

  const auto text_workflow = to::app::detail::load_request_workflow(
      {.kind = to::app::RequestFileKind::WorkflowText, .path = "workflow.txt"}, temp_directory.path(), logger);
  ASSERT_TRUE(text_workflow.has_value());
  EXPECT_EQ("detail_text", text_workflow->id);

  EXPECT_TRUE(to::app::detail::handle_submit_yaml("-", temp_directory.path(), runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_submit_text("-", temp_directory.path(), runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_submit_yaml("missing.yaml", temp_directory.path(), runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_submit_text("missing.txt", temp_directory.path(), runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_submit_yaml("workflow.yaml", temp_directory.path(), runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_submit_text("workflow.txt", temp_directory.path(), runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_reorchestrate("", runtime_service, {}, logger));
  EXPECT_TRUE(to::app::detail::handle_reorchestrate("detail_yaml", runtime_service, {}, logger));

  const to::app::detail::ServiceEndpoints endpoints{
      .http = "http://127.0.0.1:8080",
      .grpc = "grpc://127.0.0.1:9090",
  };
  EXPECT_TRUE(to::app::detail::handle_cli_command("", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(
      to::app::detail::handle_cli_command("help", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(
      to::app::detail::handle_cli_command("status", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(to::app::detail::handle_cli_command(
      "submit-yaml", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(to::app::detail::handle_cli_command(
      "submit-text", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(to::app::detail::handle_cli_command(
      "reorchestrate", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(to::app::detail::handle_cli_command(
      "submit-yaml workflow.yaml", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(to::app::detail::handle_cli_command(
      "submit-text workflow.txt", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(to::app::detail::handle_cli_command(
      "reorchestrate detail_yaml", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_TRUE(
      to::app::detail::handle_cli_command("unknown", temp_directory.path(), runtime_service, {}, endpoints, logger));
  EXPECT_FALSE(
      to::app::detail::handle_cli_command("exit", temp_directory.path(), runtime_service, {}, endpoints, logger));
}

TEST(ApplicationDetailTest, RuntimeServiceDetailHelpersConvertWorkflowResponsesAndEvents) {
  namespace pb = to::protocol::pb;

  pb::WorkflowConfig workflow;
  workflow.set_id("runtime_detail");
  workflow.mutable_optimization()->set_backend("indexed_exact");
  workflow.mutable_optimization()->set_time_limit_ms(250);
  workflow.mutable_optimization()->set_relative_gap_limit(0.1);
  workflow.mutable_optimization()->set_num_search_workers(2);
  workflow.mutable_optimization()->set_allow_partial_plan(false);
  workflow.mutable_optimization()->mutable_objective()->set_priority_weight(7);

  auto* actor = workflow.add_actors();
  actor->set_id("robot_1");
  actor->set_type("robot");
  actor->set_capacity(2);
  actor->add_capabilities("scan");
  actor->set_execution_cost_per_unit(1.5);
  actor->add_windows()->set_start(0);
  actor->mutable_windows(0)->set_end(50);

  auto* task = workflow.add_tasks();
  task->set_id("pick");
  task->set_requested_time(10);
  task->set_duration(5);
  task->set_latest_start_time(12);
  task->set_deadline(30);
  task->set_priority(9);
  task->set_demand(2);
  task->set_mandatory(false);
  task->set_preemptible(true);
  task->add_allowed_actor_types("robot");
  task->add_allowed_actor_ids("robot_1");
  task->add_preferred_actor_ids("robot_1");
  task->add_required_capabilities("scan");
  task->add_dependency_task_ids("prep");
  task->add_mutually_exclusive_task_ids("charge");
  auto* actor_distance = task->add_actor_distances();
  actor_distance->set_actor_id("robot_1");
  actor_distance->set_distance(3);
  task->set_tardiness_cost_per_unit(1.2);
  task->set_early_start_bonus(0.4);
  task->add_phase_durations(2);
  task->add_phase_durations(3);

  const to::app::WorkflowConfig app_workflow = to::app::detail::to_app_workflow(workflow);
  ASSERT_EQ(1U, app_workflow.actors.size());
  ASSERT_EQ(1U, app_workflow.tasks.size());
  EXPECT_EQ("runtime_detail", app_workflow.id);
  EXPECT_EQ("indexed_exact", app_workflow.optimization.backend);
  EXPECT_EQ(2, app_workflow.optimization.num_search_workers);
  EXPECT_EQ(7, app_workflow.optimization.objective.priority_weight);
  EXPECT_EQ(2, app_workflow.actors.front().capacity);
  EXPECT_EQ(1.5, app_workflow.actors.front().execution_cost_per_unit);
  EXPECT_EQ(std::vector<to::Duration>({2, 3}), app_workflow.tasks.front().phase_durations);
  EXPECT_EQ(std::vector<std::string>({"prep"}), app_workflow.tasks.front().dependency_task_ids);
  EXPECT_EQ(3, app_workflow.tasks.front().actor_distances.at("robot_1"));

  to::app::WorkflowConfig dependency_workflow = app_workflow;
  dependency_workflow.tasks.front().dependency_task_ids.emplace_back("pick");
  to::app::detail::remove_completed_task_dependencies(dependency_workflow, "pick");
  EXPECT_EQ(std::vector<std::string>({"prep"}), dependency_workflow.tasks.front().dependency_task_ids);
  EXPECT_EQ(5, to::app::detail::task_duration_for_id(app_workflow, "pick"));
  EXPECT_EQ(0, to::app::detail::task_duration_for_id(app_workflow, "missing"));

  const to::app::RunResult run_result{
      .ok = true,
      .capacity_issue = true,
      .assignments = {{
          .task_id = "pick",
          .actor_id = "robot_1",
          .start_time = 10,
      }},
      .unfulfilled_task_ids = {"charge"},
      .error_message = "capacity issue",
  };

  pb::RunResult proto_result;
  to::app::detail::populate_proto_run_result(run_result, app_workflow, &proto_result);
  ASSERT_EQ(1, proto_result.assignments_size());
  EXPECT_EQ("pick", proto_result.assignments(0).task_id());
  EXPECT_EQ(15, proto_result.assignments(0).end_time());
  ASSERT_EQ(1, proto_result.unfulfilled_task_ids_size());
  EXPECT_EQ("charge", proto_result.unfulfilled_task_ids(0));

  const to::protocol::RuntimeApiResponse runtime_response =
      to::app::detail::make_runtime_response(run_result, app_workflow);
  EXPECT_TRUE(runtime_response.ok());
  EXPECT_EQ("capacity issue", runtime_response.error_message());
  ASSERT_EQ(1, runtime_response.result().assignments_size());
  EXPECT_EQ(15, runtime_response.result().assignments(0).end_time());

  const to::protocol::RuntimeApiResponse runtime_error = to::app::detail::make_runtime_error_response("runtime failed");
  EXPECT_FALSE(runtime_error.ok());
  EXPECT_EQ("runtime failed", runtime_error.error_message());
  EXPECT_FALSE(runtime_error.result().ok());

  const to::protocol::WorkflowEvent accepted =
      to::app::detail::make_event(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, "runtime_detail", "accepted");
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, accepted.type());
  EXPECT_EQ("runtime_detail", accepted.workflow_id());
  EXPECT_EQ("accepted", accepted.detail());

  const to::protocol::WorkflowEvent finished = to::app::detail::make_response_event(
      pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, "runtime_detail", runtime_response, "done");
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, finished.type());
  ASSERT_TRUE(finished.has_response());
  EXPECT_TRUE(finished.response().ok());

  const to::protocol::RuntimeApiResponse consumed =
      to::app::detail::consume_final_response(make_runtime_service_response_stream(runtime_response));
  EXPECT_TRUE(consumed.ok());
  EXPECT_EQ("capacity issue", consumed.error_message());
}

TEST(ApplicationDetailTest, RunModeHelpersHandleDirectErrorPaths) {
  const ScopedTempDirectory temp_directory;
  const auto logger = to::app::detail::application_logger();

  EXPECT_EQ(EXIT_FAILURE, to::app::detail::run_one_shot_request({}, temp_directory.path(), logger));
  EXPECT_EQ(EXIT_FAILURE,
            to::app::detail::run_service_mode(
                {
                    .configured = true,
                    .security = {},
                    .interfaces = {},
                    .control_plane = {},
                    .bootstrap_request = {},
                    .bootstrap_workflow = std::nullopt,
                },
                temp_directory.path(),
                logger));

  EXPECT_EQ(EXIT_FAILURE,
            to::app::detail::run_service_mode(
                {
                    .configured = true,
                    .security = {},
                    .interfaces =
                        {
                            .cli =
                                {
                                    .enabled = true,
                                    .prompt = "direct> ",
                                },
                            .http = {},
                            .grpc = {},
                        },
                    .control_plane = {},
                    .bootstrap_request =
                        {
                            .kind = to::app::RequestFileKind::WorkflowYaml,
                            .path = "missing.yaml",
                        },
                    .bootstrap_workflow = std::nullopt,
                },
                temp_directory.path(),
                logger));
}

TEST(ApplicationDetailTest, RunCliLoopStopsWhenInputCloses) {
  const ScopedTempDirectory temp_directory;
  const ScopedStdinPipe stdin_pipe("help\nquit\n");
  ASSERT_TRUE(stdin_pipe.active());

  to::app::InMemoryWorkflowRuntimeService runtime_service(to::protocol::SecurityConfig{});
  const auto logger = to::app::detail::application_logger();

  EXPECT_EQ(EXIT_SUCCESS,
            to::app::detail::run_cli_loop(temp_directory.path(),
                                          {
                                              .enabled = true,
                                              .prompt = "loop> ",
                                          },
                                          {},
                                          {
                                              .http = {},
                                              .grpc = {},
                                          },
                                          runtime_service,
                                          logger));
}

TEST(ApplicationDetailTest, RunCliLoopProcessesQuitCommandFromReadyInput) {
  const ScopedTempDirectory temp_directory;
  const ScopedStdinPipe stdin_pipe("quit\n", false);
  ASSERT_TRUE(stdin_pipe.active());

  to::app::InMemoryWorkflowRuntimeService runtime_service(to::protocol::SecurityConfig{});
  const auto logger = to::app::detail::application_logger();

  EXPECT_EQ(EXIT_SUCCESS,
            to::app::detail::run_cli_loop(temp_directory.path(),
                                          {
                                              .enabled = true,
                                              .prompt = "ready> ",
                                          },
                                          {},
                                          {
                                              .http = {},
                                              .grpc = {},
                                          },
                                          runtime_service,
                                          logger));
}

TEST(ApplicationDetailTest, RunCliLoopStopsOnInterruptAfterPollingTimeout) {
  const ScopedTempDirectory temp_directory;
  const ScopedStdinPipe stdin_pipe("", false);
  ASSERT_TRUE(stdin_pipe.active());

  to::app::InMemoryWorkflowRuntimeService runtime_service(to::protocol::SecurityConfig{});
  const auto logger = to::app::detail::application_logger();
  to::app::detail::install_signal_handlers();

  std::jthread signal_thread([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ::raise(SIGINT);
  });

  EXPECT_EQ(EXIT_SUCCESS,
            to::app::detail::run_cli_loop(temp_directory.path(),
                                          {
                                              .enabled = true,
                                              .prompt = "interrupt> ",
                                          },
                                          {},
                                          {
                                              .http = {},
                                              .grpc = {},
                                          },
                                          runtime_service,
                                          logger));
}

}  // namespace
