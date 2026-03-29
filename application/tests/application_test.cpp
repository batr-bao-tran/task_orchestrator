#include "runner/application.hpp"

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

namespace {
namespace to = task_orchestrator;

class ScopedTempDirectory final {
 public:
  ScopedTempDirectory() {
    const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::filesystem::file_time_type::clock::now().time_since_epoch())
                               .count();
    const auto unique_suffix = std::to_string(::getpid()) + "_" + std::to_string(static_cast<long long>(timestamp));
    path_ = std::filesystem::temp_directory_path() / ("task_orchestrator_application_test_" + unique_suffix);
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
  explicit ScopedStdinPipe(std::string_view content) {
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
    const auto bytes_written = ::write(pipe_descriptors[1], content.data(), content.size());
    if (bytes_written < 0 || static_cast<std::size_t>(bytes_written) != content.size()) {
      close_pipe_descriptors(pipe_descriptors[0], pipe_descriptors[1]);
      restore_stdin();
      return;
    }

    ::close(pipe_descriptors[1]);
    read_descriptor_ = pipe_descriptors[0];
    active_ = true;
  }

  ~ScopedStdinPipe() noexcept {
    if (read_descriptor_ >= 0) {
      ::close(read_descriptor_);
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
  bool active_ = false;
};

std::filesystem::path write_file(const std::filesystem::path& path, std::string_view content) {
  std::ofstream output(path, std::ios::binary);
  output << content;
  output.close();
  return path;
}

std::filesystem::path runfiles_path(std::string_view relative_path) {
  const char* const test_srcdir = std::getenv("TEST_SRCDIR");
  const char* const test_workspace = std::getenv("TEST_WORKSPACE");
  EXPECT_NE(nullptr, test_srcdir);
  EXPECT_NE(nullptr, test_workspace);
  return std::filesystem::path(test_srcdir) / test_workspace / relative_path;
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
    requested_time: 0
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
- task pack duration 5 release 0 deadline 30 priority 5 requires_type robot depends_on pick
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

TEST(ApplicationTest, RunFromArgsRejectsMissingConfigPath) {
  const to::app::Application application;
  std::string executable_name = "run_config";
  std::array<char*, 1> argv = {executable_name.data()};

  EXPECT_EQ(EXIT_FAILURE, application.run_from_args(static_cast<int>(argv.size()), argv.data()));
}

TEST(ApplicationTest, RunFromArgsExecutesOneShotWorkflowYamlFromFile) {
  const ScopedTempDirectory temp_directory;
  const std::filesystem::path workflow_path =
      write_file(temp_directory.path() / "workflow.yaml", workflow_yaml("cli_yaml"));
  const std::filesystem::path application_config_path = write_file(temp_directory.path() / "application.yaml",
                                                                   "application:\n"
                                                                   "  mode: one_shot\n"
                                                                   "  request:\n"
                                                                   "    kind: workflow_yaml\n"
                                                                   "    path: " +
                                                                       workflow_path.filename().string() + "\n");

  const to::app::Application application;
  std::string executable_name = "run_config";
  std::string config_path_string = application_config_path.string();
  std::array<char*, 2> argv = {executable_name.data(), config_path_string.data()};

  EXPECT_EQ(EXIT_SUCCESS, application.run_from_args(static_cast<int>(argv.size()), argv.data()));
}

TEST(ApplicationTest, RunFromArgsExecutesExampleOneShotApplicationConfig) {
  const std::filesystem::path application_config_path =
      runfiles_path("application/examples/application_configs/one_shot_text_request.yaml");

  const to::app::Application application;
  std::string executable_name = "run_config";
  std::string config_path_string = application_config_path.string();
  std::array<char*, 2> argv = {executable_name.data(), config_path_string.data()};

  EXPECT_EQ(EXIT_SUCCESS, application.run_from_args(static_cast<int>(argv.size()), argv.data()));
}

TEST(ApplicationTest, RunExecutesRelativeWorkflowTextRequestInOneShotMode) {
  const ScopedTempDirectory temp_directory;
  write_file(temp_directory.path() / "request.txt", workflow_text("text_demo"));

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::OneShot,
      .request =
          {
              .kind = to::app::RequestFileKind::WorkflowText,
              .path = "request.txt",
          },
      .service = {},
  };

  const to::app::Application application;
  EXPECT_EQ(EXIT_SUCCESS, application.run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunFromFileRejectsMissingAndInvalidApplicationConfigs) {
  const ScopedTempDirectory temp_directory;
  const std::filesystem::path invalid_config_path =
      write_file(temp_directory.path() / "invalid_application.yaml", "workflow:\n  id: not-an-app\n");

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run_from_file((temp_directory.path() / "missing.yaml").c_str()));
  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run_from_file(invalid_config_path.c_str()));
}

TEST(ApplicationTest, RunFromFileReadsApplicationConfigFromStdin) {
  const ScopedTempDirectory temp_directory;
  const std::filesystem::path workflow_path =
      write_file(temp_directory.path() / "stdin_workflow.yaml", workflow_yaml("stdin_yaml"));
  const ScopedStdinPipe stdin_pipe(
      "application:\n"
      "  mode: one_shot\n"
      "  request:\n"
      "    kind: workflow_yaml\n"
      "    path: " +
      workflow_path.string() + "\n");
  ASSERT_TRUE(stdin_pipe.active());

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run_from_file("-"));
}

TEST(ApplicationTest, RunRejectsIncompleteConfigsForSelectedMode) {
  const to::app::ApplicationConfig missing_request_config{
      .configured = true,
      .mode = to::app::ApplicationMode::OneShot,
      .request = {},
      .service = {},
  };
  const to::app::ApplicationConfig missing_interface_config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces = {},
              .bootstrap_request = {},
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(missing_request_config));
  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(missing_interface_config));
}

TEST(ApplicationTest, RunOneShotModeRejectsMissingOrUnreadableRequests) {
  const ScopedTempDirectory temp_directory;

  const to::app::ApplicationConfig missing_request_config{
      .configured = true,
      .mode = to::app::ApplicationMode::OneShot,
      .request = {},
      .service = {},
  };
  const to::app::ApplicationConfig missing_text_file_config{
      .configured = true,
      .mode = to::app::ApplicationMode::OneShot,
      .request =
          {
              .kind = to::app::RequestFileKind::WorkflowText,
              .path = "missing.txt",
          },
      .service = {},
  };
  const to::app::ApplicationConfig missing_yaml_content_config{
      .configured = true,
      .mode = to::app::ApplicationMode::OneShot,
      .request =
          {
              .kind = to::app::RequestFileKind::WorkflowYaml,
              .path = "missing_workflow.yaml",
          },
      .service = {},
  };

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(missing_request_config, temp_directory.path()));
  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(missing_text_file_config, temp_directory.path()));
  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(missing_yaml_content_config, temp_directory.path()));
}

TEST(ApplicationTest, RunOneShotModeReturnsFailureForCapacityIssue) {
  const ScopedTempDirectory temp_directory;
  const std::filesystem::path workflow_path =
      write_file(temp_directory.path() / "impossible.yaml", workflow_yaml("impossible"));
  (void)workflow_path;

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::OneShot,
      .request =
          {
              .kind = to::app::RequestFileKind::WorkflowYaml,
              .path = "impossible.yaml",
          },
      .service = {},
  };

  write_file(temp_directory.path() / "impossible.yaml",
             "id: impossible\n"
             "optimization:\n"
             "  backend: indexed_exact\n"
             "actors:\n"
             "  - id: robot_1\n"
             "    type: robot\n"
             "    capacity: 1\n"
             "    windows:\n"
             "      - start: 0\n"
             "        end: 10\n"
             "tasks:\n"
             "  - id: impossible_task\n"
             "    requested_time: 0\n"
             "    duration: 20\n"
             "    deadline: 5\n"
             "    priority: 1\n"
             "    allowed_actor_types: [robot]\n");

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(config, temp_directory.path()));
}

class ApplicationAuthModeTest : public ::testing::TestWithParam<std::pair<to::protocol::AuthMode, std::string>> {};

TEST_P(ApplicationAuthModeTest, RunServeModeSupportsBootstrapWorkflowWithConfiguredAuth) {
  const ScopedTempDirectory temp_directory;
  const ScopedStdinPipe stdin_pipe("exit\n");
  ASSERT_TRUE(stdin_pipe.active());

  const auto [auth_mode, expected_credential] = GetParam();
  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security =
                  {
                      .mode = auth_mode,
                      .expected_credential = expected_credential,
                      .require_secure_transport = false,
                  },
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "auth> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request = {},
              .bootstrap_workflow = rich_workflow_config("bootstrap_direct"),
          },
  };

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run(config, temp_directory.path()));
}

INSTANTIATE_TEST_SUITE_P(AuthModes,
                         ApplicationAuthModeTest,
                         ::testing::Values(std::make_pair(to::protocol::AuthMode::BearerToken, "bearer-secret"),
                                           std::make_pair(to::protocol::AuthMode::ApiKey, "api-key-secret")));

TEST(ApplicationTest, RunServeModeRejectsMissingBootstrapWorkflowRequest) {
  const ScopedTempDirectory temp_directory;

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "bootstrap> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request =
                  {
                      .kind = to::app::RequestFileKind::WorkflowYaml,
                      .path = "missing_bootstrap.yaml",
                  },
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeContinuesServingWhenBootstrapRequestHasCapacityIssue) {
  const ScopedTempDirectory temp_directory;
  write_file(temp_directory.path() / "invalid_bootstrap.yaml",
             "id: invalid_bootstrap\n"
             "optimization:\n"
             "  backend: indexed_exact\n"
             "actors: []\n"
             "tasks:\n"
             "  - id: impossible_task\n"
             "    requested_time: 0\n"
             "    duration: 5\n"
             "    deadline: 1\n"
             "    priority: 1\n");

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "bootstrap-fail> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request =
                  {
                      .kind = to::app::RequestFileKind::WorkflowYaml,
                      .path = "invalid_bootstrap.yaml",
                  },
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeContinuesServingWhenInlineBootstrapWorkflowHasCapacityIssue) {
  const ScopedTempDirectory temp_directory;

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "inline-bootstrap-fail> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request = {},
              .bootstrap_workflow =
                  to::app::WorkflowConfig{
                      .id = "inline_invalid_bootstrap",
                      .optimization =
                          {
                              .backend = "indexed_exact",
                              .objective = {},
                          },
                      .actors = {},
                      .tasks = {{
                          .id = "impossible_task",
                          .requested_time = 0,
                          .duration = 5,
                          .latest_start_time = 0,
                          .deadline = 1,
                          .priority = 1,
                          .demand = 1,
                          .mandatory = true,
                          .preemptible = false,
                          .allowed_actor_types = {},
                          .allowed_actor_ids = {},
                          .preferred_actor_ids = {},
                          .required_capabilities = {},
                          .dependency_task_ids = {},
                          .mutually_exclusive_task_ids = {},
                          .actor_distances = {},
                          .tardiness_cost_per_unit = 0.0,
                          .early_start_bonus = 0.0,
                          .phase_durations = {},
                      }},
                  },
          },
  };

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeHandlesCliUsageErrorsAndClosedInput) {
  const ScopedTempDirectory temp_directory;
  const ScopedStdinPipe stdin_pipe(
      "\n"
      " submit-yaml   \n"
      "submit-text\n"
      "reorchestrate\n"
      "submit-yaml -\n"
      "submit-text -\n"
      "exit\n");
  ASSERT_TRUE(stdin_pipe.active());

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "usage> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request = {},
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeExitsWhenCliInputCloses) {
  const ScopedTempDirectory temp_directory;
  const ScopedStdinPipe stdin_pipe("");
  ASSERT_TRUE(stdin_pipe.active());

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "closed> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request = {},
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeProcessesCliCommandsAndBootstrapWorkflow) {
  const ScopedTempDirectory temp_directory;
  write_file(temp_directory.path() / "bootstrap.yaml", workflow_yaml("bootstrap_cli"));
  write_file(temp_directory.path() / "submit.yaml", workflow_yaml("submitted_yaml"));
  write_file(temp_directory.path() / "submit.txt", workflow_text("submitted_text"));

  const ScopedStdinPipe stdin_pipe(
      "status\n"
      "help\n"
      "submit-yaml submit.yaml\n"
      "submit-text submit.txt\n"
      "reorchestrate bootstrap_cli\n"
      "unknown\n"
      "quit\n");
  ASSERT_TRUE(stdin_pipe.active());

  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli =
                          {
                              .enabled = true,
                              .prompt = "test> ",
                          },
                      .http = {},
                      .grpc = {},
                  },
              .bootstrap_request =
                  {
                      .kind = to::app::RequestFileKind::WorkflowYaml,
                      .path = "bootstrap.yaml",
                  },
              .bootstrap_workflow = std::nullopt,
          },
  };

  const to::app::Application application;
  EXPECT_EQ(EXIT_SUCCESS, application.run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeExecutesExampleServeApplicationConfig) {
  const ScopedStdinPipe stdin_pipe("status\nquit\n");
  ASSERT_TRUE(stdin_pipe.active());

  const std::filesystem::path application_config_path =
      runfiles_path("application/examples/application_configs/serve_http_grpc_cli.yaml");

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run_from_file(application_config_path.c_str()));
}

TEST(ApplicationTest, RunServeModeFailsWhenHttpServerCannotStart) {
  const ScopedTempDirectory temp_directory;
  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli = {},
                      .http =
                          {
                              .enabled = true,
                              .endpoint =
                                  {
                                      .bind_address = "not-an-ip-address",
                                      .port = 0,
                                      .tls = {},
                                  },
                          },
                      .grpc = {},
                  },
              .bootstrap_request = {},
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunServeModeStopsHttpServerWhenGrpcServerCannotStart) {
  const ScopedTempDirectory temp_directory;
  const to::app::ApplicationConfig config{
      .configured = true,
      .mode = to::app::ApplicationMode::Serve,
      .request = {},
      .service =
          {
              .configured = true,
              .security = {},
              .interfaces =
                  {
                      .cli = {},
                      .http =
                          {
                              .enabled = true,
                              .endpoint =
                                  {
                                      .bind_address = "127.0.0.1",
                                      .port = 0,
                                      .tls = {},
                                  },
                          },
                      .grpc =
                          {
                              .enabled = true,
                              .endpoint =
                                  {
                                      .bind_address = "not-an-ip-address",
                                      .port = 0,
                                      .tls = {},
                                  },
                          },
                  },
              .bootstrap_request = {},
              .bootstrap_workflow = std::nullopt,
          },
  };

  EXPECT_EQ(EXIT_FAILURE, to::app::Application::run(config, temp_directory.path()));
}

TEST(ApplicationTest, RunFromArgsServeModeStartsTransportEndpointsAndStopsOnSignal) {
  const ScopedTempDirectory temp_directory;
  const std::filesystem::path application_config_path = write_file(temp_directory.path() / "serve.yaml",
                                                                   "application:\n"
                                                                   "  mode: serve\n"
                                                                   "  service:\n"
                                                                   "    interfaces:\n"
                                                                   "      http:\n"
                                                                   "        enabled: true\n"
                                                                   "        bind_address: 127.0.0.1\n"
                                                                   "        port: 0\n"
                                                                   "      grpc:\n"
                                                                   "        enabled: true\n"
                                                                   "        bind_address: 127.0.0.1\n"
                                                                   "        port: 0\n");

  std::jthread signal_thread([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ::raise(SIGINT);
  });

  std::string executable_name = "run_config";
  std::string config_path_string = application_config_path.string();
  std::array<char*, 2> argv = {executable_name.data(), config_path_string.data()};

  EXPECT_EQ(EXIT_SUCCESS, to::app::Application::run_from_args(static_cast<int>(argv.size()), argv.data()));
}

}  // namespace
