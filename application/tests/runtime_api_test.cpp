#include "protocol/runtime_api.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "protocol/grpc_transport.hpp"
#include "protocol/http_transport.hpp"
#include "protocol/tls_credentials.hpp"
#include "runtime_service/in_memory_runtime_service.hpp"
#include "test_support_tls_material.hpp"

namespace {
namespace to = task_orchestrator;
namespace tp = task_orchestrator::protocol;
namespace pb = task_orchestrator::protocol::pb;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using TestTlsServerStream = beast::ssl_stream<tcp::socket>;

inline constexpr auto kTransportFailurePropagationDelay = std::chrono::milliseconds(100);

const tp::test_support::TestTlsMaterial& test_tls_material() noexcept {
  return tp::test_support::localhost_tls_material();
}

pb::TaskConfig make_task(
    std::string id, to::Time requested_time, to::Duration duration, to::Time deadline, to::Priority priority) {
  pb::TaskConfig task;
  task.set_id(std::move(id));
  task.set_requested_time(requested_time);
  task.set_duration(duration);
  task.set_latest_start_time(0);
  task.set_deadline(deadline);
  task.set_priority(priority);
  task.set_demand(1);
  task.set_mandatory(true);
  task.set_preemptible(false);
  task.add_allowed_actor_types("robot");
  return task;
}

pb::WorkflowConfig make_workflow() {
  pb::WorkflowConfig workflow;
  workflow.set_id("runtime_demo");
  workflow.mutable_optimization()->set_backend("indexed_exact");
  workflow.mutable_optimization()->set_num_search_workers(1);
  workflow.mutable_optimization()->set_allow_partial_plan(true);

  pb::ActorConfig* actor = workflow.add_actors();
  actor->set_id("robot_1");
  actor->set_type("robot");
  actor->set_capacity(1);
  actor->add_capabilities("lift");
  actor->set_execution_cost_per_unit(0.0);
  pb::AvailabilityWindow* window = actor->add_windows();
  window->set_start(0);
  window->set_end(100);

  pb::TaskConfig pick = make_task("pick", 0, 5, 20, 10);
  pick.add_preferred_actor_ids("robot_1");
  workflow.add_tasks()->CopyFrom(pick);

  pb::TaskConfig pack = make_task("pack", 0, 5, 30, 5);
  pack.add_dependency_task_ids("pick");
  workflow.add_tasks()->CopyFrom(pack);
  return workflow;
}

pb::ClientAuthContext make_auth(std::string bearer_token = {},
                                std::string api_key = {},
                                bool secure_transport = false) {
  pb::ClientAuthContext auth;
  auth.set_bearer_token(std::move(bearer_token));
  auth.set_api_key(std::move(api_key));
  auth.set_secure_transport(secure_transport);
  return auth;
}

tp::SubmitWorkflowRequest make_submit_request(const pb::ClientAuthContext& auth = {}, bool replace_existing = true) {
  tp::SubmitWorkflowRequest request;
  request.mutable_config()->CopyFrom(make_workflow());
  request.mutable_auth()->CopyFrom(auth);
  request.set_replace_existing(replace_existing);
  return request;
}

tp::ReorchestrateRequest make_reorchestrate_request(std::string workflow_id, bool trigger_reorchestration = true) {
  tp::ReorchestrateRequest request;
  request.set_workflow_id(std::move(workflow_id));
  request.set_trigger_reorchestration(trigger_reorchestration);
  return request;
}

std::vector<tp::WorkflowEvent> collect_events(tp::WorkflowEventStream event_stream) {
  std::vector<tp::WorkflowEvent> events;
  for (tp::WorkflowEvent event : event_stream) {
    events.push_back(std::move(event));
  }
  return events;
}

tp::TlsServerConfig make_server_tls_config() {
  return tp::TlsServerConfig{
      .identity =
          {
              .certificate_chain = tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
              .private_key = tp::TlsDataSource::from_inline_pem(test_tls_material().private_key_pem),
              .private_key_password = {},
          },
      .client_trust =
          {
              .root_certificates = {},
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = "",
          },
      .require_client_certificate = false,
  };
}

tp::TlsClientConfig make_client_tls_config(std::string expected_peer_name = "localhost") {
  return tp::TlsClientConfig{
      .identity = {},
      .server_trust =
          {
              .root_certificates = tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = std::move(expected_peer_name),
          },
  };
}

std::filesystem::path write_temp_pem_file(std::string_view file_name, std::string_view pem_contents) {
  const std::filesystem::path file_path = std::filesystem::temp_directory_path() / file_name;
  std::ofstream file(file_path, std::ios::binary);
  file << pem_contents;
  file.close();
  return file_path;
}

int endpoint_port(std::string_view endpoint) {
  const std::size_t last_colon = endpoint.rfind(':');
  EXPECT_NE(std::string_view::npos, last_colon);
  return std::stoi(std::string(endpoint.substr(last_colon + 1)));
}

// NOLINTBEGIN(bugprone-unused-return-value)

int reserve_unused_port() {
  asio::io_context io_context;
  tcp::acceptor acceptor(io_context);
  beast::error_code error_code;
  acceptor.open(tcp::v4(), error_code);
  EXPECT_FALSE(error_code);
  acceptor.bind(tcp::endpoint(asio::ip::make_address(tp::kDefaultLoopbackAddress), 0), error_code);
  EXPECT_FALSE(error_code);
  const int port = acceptor.local_endpoint(error_code).port();
  EXPECT_FALSE(error_code);
  acceptor.close(error_code);
  EXPECT_FALSE(error_code);
  return port;
}

template <typename Message>
std::string serialize_proto(const Message& message) {
  std::string body;
  EXPECT_TRUE(message.SerializeToString(&body));
  return body;
}

tp::RuntimeApiResponse parse_runtime_response(const http::response<http::string_body>& response) {
  tp::RuntimeApiResponse parsed_response;
  EXPECT_TRUE(parsed_response.ParseFromString(response.body()));
  return parsed_response;
}

http::response<http::string_body> send_http_request(int port,
                                                    http::verb method,
                                                    const std::string& target,
                                                    std::string body = {},
                                                    const std::string& bearer_token = {},
                                                    const std::string& api_key = {}) {
  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  beast::tcp_stream stream(io_context);

  beast::error_code error_code;
  const auto endpoints = resolver.resolve(tp::kDefaultLoopbackAddress, std::to_string(port), error_code);
  EXPECT_FALSE(error_code);
  stream.connect(endpoints, error_code);
  EXPECT_FALSE(error_code);

  http::request<http::string_body> request(method, std::move(target), 11);
  request.set(http::field::host, tp::kDefaultLoopbackAddress);
  request.set(http::field::user_agent, "runtime_api_test");
  request.set(http::field::accept, std::string(tp::kBinaryProtoContentType));
  request.set(http::field::content_type, std::string(tp::kBinaryProtoContentType));
  if (!bearer_token.empty()) {
    request.set(std::string(tp::kAuthorizationHeader), "Bearer " + bearer_token);
  }
  if (!api_key.empty()) {
    request.set(std::string(tp::kApiKeyHeader), api_key);
  }
  request.body() = std::move(body);
  request.prepare_payload();

  http::write(stream, request, error_code);
  EXPECT_FALSE(error_code);

  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response, error_code);
  EXPECT_FALSE(error_code);

  stream.socket().shutdown(tcp::socket::shutdown_both, error_code);
  return response;
}

struct OneShotServerHandle {
  int port = 0;
  std::jthread thread;
};

template <typename Handler>
OneShotServerHandle start_one_shot_tcp_server(Handler handler) {
  auto io_context = std::make_shared<asio::io_context>(1);
  auto acceptor = std::make_shared<tcp::acceptor>(*io_context);
  beast::error_code error_code;
  acceptor->open(tcp::v4(), error_code);
  EXPECT_FALSE(error_code);
  acceptor->set_option(asio::socket_base::reuse_address(true), error_code);
  EXPECT_FALSE(error_code);
  acceptor->bind(tcp::endpoint(asio::ip::make_address(tp::kDefaultLoopbackAddress), 0), error_code);
  EXPECT_FALSE(error_code);
  acceptor->listen(asio::socket_base::max_listen_connections, error_code);
  EXPECT_FALSE(error_code);
  const int port = acceptor->local_endpoint(error_code).port();
  EXPECT_FALSE(error_code);

  return OneShotServerHandle{
      .port = port,
      .thread = std::jthread([io_context, acceptor, handler = std::move(handler)]() mutable {
        tcp::socket socket(*io_context);
        beast::error_code accept_error;
        acceptor->accept(socket, accept_error);
        if (accept_error) {
          return;
        }
        handler(std::move(socket));
      }),
  };
}

std::shared_ptr<ssl::context> make_test_tls_server_context() {
  EXPECT_TRUE(test_tls_material().ok) << test_tls_material().error_message;
  auto context = std::make_shared<ssl::context>(ssl::context::tls_server);
  beast::error_code error_code;
  context->use_certificate_chain(asio::buffer(test_tls_material().certificate_chain_pem), error_code);
  EXPECT_FALSE(error_code);
  context->use_private_key(asio::buffer(test_tls_material().private_key_pem), ssl::context::pem, error_code);
  EXPECT_FALSE(error_code);
  return context;
}

template <typename Handler>
OneShotServerHandle start_one_shot_tls_server(Handler handler) {
  auto io_context = std::make_shared<asio::io_context>(1);
  auto acceptor = std::make_shared<tcp::acceptor>(*io_context);
  auto tls_context = make_test_tls_server_context();
  beast::error_code error_code;
  acceptor->open(tcp::v4(), error_code);
  EXPECT_FALSE(error_code);
  acceptor->set_option(asio::socket_base::reuse_address(true), error_code);
  EXPECT_FALSE(error_code);
  acceptor->bind(tcp::endpoint(asio::ip::make_address(tp::kDefaultLoopbackAddress), 0), error_code);
  EXPECT_FALSE(error_code);
  acceptor->listen(asio::socket_base::max_listen_connections, error_code);
  EXPECT_FALSE(error_code);
  const int port = acceptor->local_endpoint(error_code).port();
  EXPECT_FALSE(error_code);

  return OneShotServerHandle{
      .port = port,
      .thread = std::jthread([io_context, acceptor, tls_context, handler = std::move(handler)]() mutable {
        tcp::socket socket(*io_context);
        beast::error_code accept_error;
        acceptor->accept(socket, accept_error);
        if (accept_error) {
          return;
        }
        TestTlsServerStream stream(std::move(socket), *tls_context);
        beast::error_code handshake_error;
        stream.handshake(ssl::stream_base::server, handshake_error);
        if (handshake_error) {
          return;
        }
        handler(stream);
      }),
  };
}

void receive_some_bytes(tcp::socket& socket) {
  std::array<char, 1024> buffer{};
  beast::error_code error_code;
  socket.read_some(asio::buffer(buffer), error_code);
}

void receive_some_bytes(TestTlsServerStream& stream) {
  std::array<char, 1024> buffer{};
  beast::error_code error_code;
  stream.read_some(asio::buffer(buffer), error_code);
}

void close_with_tcp_reset(tcp::socket& socket) {
  beast::error_code error_code;
  socket.set_option(asio::socket_base::linger(true, 0), error_code);
  socket.close(error_code);
}

class StubTlsCredentialProvider final : public tp::TlsCredentialProvider {
 public:
  tp::TlsServerLoadResult load_server_credentials(const tp::TlsServerConfig&) const override { return server_result; }

  tp::TlsClientLoadResult load_client_credentials(const tp::TlsClientConfig&) const override { return client_result; }

  tp::TlsServerLoadResult server_result;
  tp::TlsClientLoadResult client_result;
};

struct ServerTlsFailureCase {
  std::string name;
  tp::TlsServerConfig config;
  std::string expected_error_substring;
};

struct ClientTlsFailureCase {
  std::string name;
  tp::TlsClientConfig config;
  std::string expected_error_substring;
};

class StaticServerTlsCredentialFailureParamTest : public ::testing::TestWithParam<ServerTlsFailureCase> {};

class StaticClientTlsCredentialFailureParamTest : public ::testing::TestWithParam<ClientTlsFailureCase> {};

struct AuthCase {
  std::string name;
  tp::SecurityConfig security;
  pb::ClientAuthContext auth;
  bool expected_ok = false;
};

class RuntimeApiAuthParamTest : public ::testing::TestWithParam<AuthCase> {};

TEST_P(RuntimeApiAuthParamTest, SubmitWorkflowHonorsSecurityModes) {
  const AuthCase& test_case = GetParam();
  to::app::InMemoryWorkflowRuntimeService service(test_case.security);

  const tp::RuntimeApiResponse response = service.submit_workflow(make_submit_request(test_case.auth));

  EXPECT_EQ(test_case.expected_ok, response.ok()) << response.error_message();
}

INSTANTIATE_TEST_SUITE_P(
    RuntimeAuth,
    RuntimeApiAuthParamTest,
    ::testing::Values(
        AuthCase{
            .name = "no_auth",
            .security = {.mode = tp::AuthMode::None, .expected_credential = "", .require_secure_transport = false},
            .auth = make_auth(),
            .expected_ok = true,
        },
        AuthCase{
            .name = "bearer_success",
            .security = {.mode = tp::AuthMode::BearerToken,
                         .expected_credential = "token-123",
                         .require_secure_transport = true},
            .auth = make_auth("token-123", "", true),
            .expected_ok = true,
        },
        AuthCase{
            .name = "api_key_failure",
            .security = {.mode = tp::AuthMode::ApiKey,
                         .expected_credential = "key-123",
                         .require_secure_transport = false},
            .auth = make_auth("", "wrong", false),
            .expected_ok = false,
        },
        AuthCase{
            .name = "secure_transport_required",
            .security = {.mode = tp::AuthMode::None, .expected_credential = "", .require_secure_transport = true},
            .auth = make_auth(),
            .expected_ok = false,
        },
        AuthCase{
            .name = "api_key_success",
            .security = {.mode = tp::AuthMode::ApiKey,
                         .expected_credential = "key-123",
                         .require_secure_transport = false},
            .auth = make_auth("", "key-123", false),
            .expected_ok = true,
        }),
    [](const ::testing::TestParamInfo<AuthCase>& info) { return info.param.name; });

TEST(RuntimeApiTest, ReorchestrateAppliesOverridesAndTriggersNewPlan) {
  to::app::InMemoryWorkflowRuntimeService service;
  const auto submit = service.submit_workflow(make_submit_request());
  ASSERT_TRUE(submit.ok());
  ASSERT_EQ(2, submit.result().assignments_size());

  auto request = make_reorchestrate_request("runtime_demo");
  pb::TaskStateOverride* task_override = request.add_task_overrides();
  task_override->set_task_id("pick");
  task_override->set_completed(true);

  const auto reorchestrated = service.reorchestrate(request);
  ASSERT_TRUE(reorchestrated.ok());
  ASSERT_EQ(1, reorchestrated.result().assignments_size());
  EXPECT_EQ("pack", reorchestrated.result().assignments(0).task_id());
}

TEST(RuntimeApiTest, SubmitWorkflowRejectsDuplicateWithoutReplace) {
  to::app::InMemoryWorkflowRuntimeService service;
  ASSERT_TRUE(service.submit_workflow(make_submit_request()).ok());

  const auto duplicate = service.submit_workflow(make_submit_request({}, false));
  EXPECT_FALSE(duplicate.ok());
  EXPECT_NE(std::string::npos, duplicate.error_message().find("Workflow already exists"));
}

TEST(RuntimeApiTest, ReorchestrateFailsForMissingWorkflow) {
  to::app::InMemoryWorkflowRuntimeService service;
  const auto response = service.reorchestrate(make_reorchestrate_request("missing"));
  EXPECT_FALSE(response.ok());
  EXPECT_NE(std::string::npos, response.error_message().find("not found"));
}

TEST(RuntimeApiTest, ReorchestrateWithoutTriggerStillAppliesOverrides) {
  to::app::InMemoryWorkflowRuntimeService service;
  ASSERT_TRUE(service.submit_workflow(make_submit_request()).ok());

  auto update_request = make_reorchestrate_request("runtime_demo", false);
  pb::TaskStateOverride* task_override = update_request.add_task_overrides();
  task_override->set_task_id("pick");
  task_override->set_requested_time(9);
  task_override->set_deadline(21);
  task_override->set_priority(42);
  task_override->set_pinned_actor_id("robot_1");
  pb::ActorStateOverride* actor_override = update_request.add_actor_overrides();
  actor_override->set_actor_id("robot_1");
  actor_override->set_unavailable(true);
  actor_override->set_capacity(3);

  const auto updated = service.reorchestrate(update_request);
  ASSERT_TRUE(updated.ok());
  EXPECT_EQ(0, updated.result().assignments_size());

  auto replanned_request = make_reorchestrate_request("runtime_demo", true);
  pb::ActorStateOverride* restore_actor_override = replanned_request.add_actor_overrides();
  restore_actor_override->set_actor_id("robot_1");
  restore_actor_override->set_unavailable(false);

  const auto replanned = service.reorchestrate(replanned_request);
  ASSERT_TRUE(replanned.ok());
  ASSERT_GT(replanned.result().assignments_size(), 0);
  EXPECT_EQ("pick", replanned.result().assignments(0).task_id());
  EXPECT_GE(replanned.result().assignments(0).start_time(), 9);
}

TEST(RuntimeApiTest, MissingOverrideTargetsAreIgnored) {
  to::app::InMemoryWorkflowRuntimeService service;
  ASSERT_TRUE(service.submit_workflow(make_submit_request()).ok());

  auto request = make_reorchestrate_request("runtime_demo");
  pb::TaskStateOverride* missing_task_override = request.add_task_overrides();
  missing_task_override->set_task_id("missing");
  missing_task_override->set_requested_time(5);
  pb::ActorStateOverride* missing_actor_override = request.add_actor_overrides();
  missing_actor_override->set_actor_id("missing");
  missing_actor_override->set_unavailable(true);

  EXPECT_TRUE(service.reorchestrate(request).ok());
}

TEST(RuntimeApiTest, StaticTlsCredentialProviderLoadsInlineAndFileSources) {
  ASSERT_TRUE(test_tls_material().ok) << test_tls_material().error_message;
  tp::StaticTlsCredentialProvider provider;
  const std::filesystem::path certificate_path =
      write_temp_pem_file("task_orchestrator_tls_test_cert.pem", test_tls_material().certificate_chain_pem);
  const std::filesystem::path key_path =
      write_temp_pem_file("task_orchestrator_tls_test_key.pem", test_tls_material().private_key_pem);

  tp::TlsServerConfig server_config{
      .identity =
          {
              .certificate_chain = tp::TlsDataSource::from_file(certificate_path.string()),
              .private_key = tp::TlsDataSource::from_file(key_path.string()),
              .private_key_password = {},
          },
      .client_trust = {},
  };
  tp::TlsClientConfig client_config = make_client_tls_config();

  const auto loaded_server_result = provider.load_server_credentials(server_config);
  const auto loaded_client_result = provider.load_client_credentials(client_config);

  ASSERT_TRUE(loaded_server_result.ok()) << loaded_server_result.error_message;
  ASSERT_TRUE(loaded_client_result.ok()) << loaded_client_result.error_message;
  EXPECT_EQ(loaded_server_result.value.identity.certificate_chain_pem, test_tls_material().certificate_chain_pem);
  EXPECT_EQ(loaded_server_result.value.identity.private_key_pem, test_tls_material().private_key_pem);
  EXPECT_EQ(loaded_client_result.value.server_trust.root_certificates_pem, test_tls_material().certificate_chain_pem);

  std::filesystem::remove(certificate_path);
  std::filesystem::remove(key_path);
}

TEST(RuntimeApiTest, StaticTlsCredentialProviderRejectsIncompleteServerIdentity) {
  tp::StaticTlsCredentialProvider provider;
  tp::TlsServerConfig invalid_config{
      .identity =
          {
              .certificate_chain = tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
              .private_key = {},
              .private_key_password = {},
          },
      .client_trust = {},
  };

  const auto result = provider.load_server_credentials(invalid_config);
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.error_message.find("certificate_chain and private_key"), std::string::npos);
}

TEST(RuntimeApiTest, StaticTlsCredentialProviderRejectsMismatchedClientIdentityPair) {
  tp::StaticTlsCredentialProvider provider;
  const auto result = provider.load_client_credentials(tp::TlsClientConfig{
      .identity =
          {
              .certificate_chain = tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
              .private_key = {},
              .private_key_password = {},
          },
      .server_trust =
          {
              .root_certificates = tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = "localhost",
          },
  });

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.error_message.find("certificate_chain and private_key together"), std::string::npos);
}

TEST_P(StaticServerTlsCredentialFailureParamTest, RejectsInvalidServerCredentialSources) {
  tp::StaticTlsCredentialProvider provider;

  const auto result = provider.load_server_credentials(GetParam().config);

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.error_message.find(GetParam().expected_error_substring), std::string::npos);
}

std::vector<ServerTlsFailureCase> make_server_tls_failure_cases() {
  return {
      ServerTlsFailureCase{
          .name = "empty_certificate_file_path",
          .config =
              {
                  .identity =
                      {
                          .certificate_chain = {.kind = tp::TlsDataSourceKind::FilePath, .value = ""},
                          .private_key = tp::TlsDataSource::from_inline_pem(test_tls_material().private_key_pem),
                          .private_key_password = {},
                      },
                  .client_trust = {},
                  .require_client_certificate = false,
              },
          .expected_error_substring = "file path is empty",
      },
      ServerTlsFailureCase{
          .name = "missing_certificate_file",
          .config =
              {
                  .identity =
                      {
                          .certificate_chain = tp::TlsDataSource::from_file("/tmp/task_orchestrator_missing_cert.pem"),
                          .private_key = tp::TlsDataSource::from_inline_pem(test_tls_material().private_key_pem),
                          .private_key_password = {},
                      },
                  .client_trust = {},
                  .require_client_certificate = false,
              },
          .expected_error_substring = "Failed to read Server certificate chain",
      },
      ServerTlsFailureCase{
          .name = "empty_inline_private_key",
          .config =
              {
                  .identity =
                      {
                          .certificate_chain =
                              tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
                          .private_key = tp::TlsDataSource::from_inline_pem(""),
                          .private_key_password = {},
                      },
                  .client_trust = {},
                  .require_client_certificate = false,
              },
          .expected_error_substring = "inline PEM is empty",
      },
      ServerTlsFailureCase{
          .name = "unsupported_password_source_kind",
          .config =
              {
                  .identity =
                      {
                          .certificate_chain =
                              tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
                          .private_key = tp::TlsDataSource::from_inline_pem(test_tls_material().private_key_pem),
                          .private_key_password = {.kind = static_cast<tp::TlsDataSourceKind>(999), .value = "unused"},
                      },
                  .client_trust = {},
                  .require_client_certificate = false,
              },
          .expected_error_substring = "Unsupported TLS data source kind",
      },
      ServerTlsFailureCase{
          .name = "mutual_tls_without_trust_roots",
          .config =
              {
                  .identity =
                      {
                          .certificate_chain =
                              tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
                          .private_key = tp::TlsDataSource::from_inline_pem(test_tls_material().private_key_pem),
                          .private_key_password = {},
                      },
                  .client_trust =
                      {
                          .root_certificates = {},
                          .use_system_default_roots = false,
                          .verify_peer = true,
                          .expected_peer_name = "",
                      },
                  .require_client_certificate = true,
              },
          .expected_error_substring = "peer verification requires root_certificates or system default roots",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(InvalidServerTlsSources,
                         StaticServerTlsCredentialFailureParamTest,
                         ::testing::ValuesIn(make_server_tls_failure_cases()),
                         [](const ::testing::TestParamInfo<ServerTlsFailureCase>& info) { return info.param.name; });

TEST_P(StaticClientTlsCredentialFailureParamTest, RejectsInvalidClientCredentialSources) {
  tp::StaticTlsCredentialProvider provider;

  const auto result = provider.load_client_credentials(GetParam().config);

  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.error_message.find(GetParam().expected_error_substring), std::string::npos);
}

std::vector<ClientTlsFailureCase> make_client_tls_failure_cases() {
  return {
      ClientTlsFailureCase{
          .name = "empty_inline_root_certificates",
          .config =
              {
                  .identity = {},
                  .server_trust =
                      {
                          .root_certificates = tp::TlsDataSource::from_inline_pem(""),
                          .use_system_default_roots = false,
                          .verify_peer = true,
                          .expected_peer_name = "localhost",
                      },
              },
          .expected_error_substring = "inline PEM is empty",
      },
      ClientTlsFailureCase{
          .name = "verify_peer_without_roots",
          .config =
              {
                  .identity = {},
                  .server_trust =
                      {
                          .root_certificates = {},
                          .use_system_default_roots = false,
                          .verify_peer = true,
                          .expected_peer_name = "localhost",
                      },
              },
          .expected_error_substring = "peer verification requires root_certificates or system default roots",
      },
      ClientTlsFailureCase{
          .name = "password_without_private_key",
          .config =
              {
                  .identity =
                      {
                          .certificate_chain = {},
                          .private_key = {},
                          .private_key_password = tp::TlsDataSource::from_inline_pem("secret"),
                      },
                  .server_trust =
                      {
                          .root_certificates =
                              tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem),
                          .use_system_default_roots = false,
                          .verify_peer = true,
                          .expected_peer_name = "localhost",
                      },
              },
          .expected_error_substring = "private_key_password requires a private_key",
      },
  };
}

INSTANTIATE_TEST_SUITE_P(InvalidClientTlsSources,
                         StaticClientTlsCredentialFailureParamTest,
                         ::testing::ValuesIn(make_client_tls_failure_cases()),
                         [](const ::testing::TestParamInfo<ClientTlsFailureCase>& info) { return info.param.name; });

TEST(RuntimeApiTest, StreamSubmitWorkflowEmitsAcceptancePlanningAndCompletionEvents) {
  to::app::InMemoryWorkflowRuntimeService service;

  std::vector<pb::WorkflowEventType> event_types;
  tp::RuntimeApiResponse final_response;
  for (const tp::WorkflowEvent& event : service.stream_submit_workflow(make_submit_request())) {
    event_types.push_back(event.type());
    if (event.has_response()) {
      final_response = event.response();
    }
  }

  EXPECT_EQ(event_types,
            (std::vector<pb::WorkflowEventType>{
                pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED,
                pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED,
                pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED,
                pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED,
                pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED,
            }));
  ASSERT_TRUE(final_response.ok());
  EXPECT_EQ(2, final_response.result().assignments_size());
}

TEST(RuntimeApiTest, StreamReorchestrateEmitsCompletionAndReplanningEvents) {
  to::app::InMemoryWorkflowRuntimeService service;
  ASSERT_TRUE(service.submit_workflow(make_submit_request()).ok());

  auto request = make_reorchestrate_request("runtime_demo");
  pb::TaskStateOverride* task_override = request.add_task_overrides();
  task_override->set_task_id("pick");
  task_override->set_completed(true);

  std::vector<pb::WorkflowEventType> event_types;
  tp::RuntimeApiResponse final_response;
  for (const tp::WorkflowEvent& event : service.stream_reorchestrate(request)) {
    event_types.push_back(event.type());
    if (event.has_response()) {
      final_response = event.response();
    }
  }

  EXPECT_EQ(event_types,
            (std::vector<pb::WorkflowEventType>{
                pb::WORKFLOW_EVENT_TYPE_TASK_COMPLETED,
                pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED,
                pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED,
                pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED,
            }));
  ASSERT_TRUE(final_response.ok());
  ASSERT_EQ(1, final_response.result().assignments_size());
  EXPECT_EQ("pack", final_response.result().assignments(0).task_id());
}

TEST(RuntimeApiTest, StreamSubmitWorkflowRejectsUnauthorizedRequestWithRequestRejectedEvent) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::BearerToken, .expected_credential = "expected-token", .require_secure_transport = false});

  std::vector<tp::WorkflowEvent> events;
  for (tp::WorkflowEvent event : service.stream_submit_workflow(make_submit_request(make_auth("wrong-token")))) {
    events.push_back(std::move(event));
  }

  ASSERT_EQ(1U, events.size());
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, events.front().type());
  ASSERT_TRUE(events.front().has_response());
  EXPECT_FALSE(events.front().response().ok());
  EXPECT_NE(events.front().response().error_message().find("authentication failed"), std::string::npos);
}

TEST(RuntimeApiTest, SubmitWorkflowAcceptsRichOptimizationConfigurationAndTaskMetadata) {
  to::app::InMemoryWorkflowRuntimeService service;
  auto request = make_submit_request();

  pb::OptimizationConfig* optimization = request.mutable_config()->mutable_optimization();
  optimization->set_time_limit_ms(250);
  optimization->set_relative_gap_limit(0.1);
  optimization->set_num_search_workers(0);
  optimization->set_allow_partial_plan(false);
  optimization->mutable_objective()->set_fulfilled_task_weight(1500);
  optimization->mutable_objective()->set_priority_weight(200);
  optimization->mutable_objective()->set_makespan_weight(-2);
  optimization->mutable_objective()->set_travel_distance_weight(-3);
  optimization->mutable_objective()->set_tardiness_weight(-50);
  optimization->mutable_objective()->set_execution_cost_weight(-4);
  optimization->mutable_objective()->set_preferred_actor_weight(25);

  pb::TaskConfig* pick = request.mutable_config()->mutable_tasks(0);
  pick->add_required_capabilities("lift");
  pick->add_allowed_actor_ids("robot_1");
  pick->add_phase_durations(2);
  pick->add_phase_durations(3);
  pick->set_tardiness_cost_per_unit(1.25);
  pick->set_early_start_bonus(0.75);
  pb::ActorDistance* actor_distance = pick->add_actor_distances();
  actor_distance->set_actor_id("robot_1");
  actor_distance->set_distance(4);

  const auto response = service.submit_workflow(request);

  ASSERT_TRUE(response.ok()) << response.error_message();
  EXPECT_EQ(2, response.result().assignments_size());
}

TEST(RuntimeApiTest, HttpTransportRejectsUnsupportedMethodsRoutesAndMalformedBodies) {
  to::app::InMemoryWorkflowRuntimeService service;
  tp::BeastHttpWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();
  ASSERT_TRUE(server.running());
  const int port = endpoint_port(server.endpoint());

  const auto method_not_allowed_response =
      send_http_request(port, http::verb::get, std::string(tp::kHttpSubmitWorkflowPath));
  EXPECT_EQ(http::status::method_not_allowed, method_not_allowed_response.result());
  EXPECT_NE(parse_runtime_response(method_not_allowed_response).error_message().find("Only POST"), std::string::npos);

  const auto malformed_submit_response =
      send_http_request(port, http::verb::post, std::string(tp::kHttpSubmitWorkflowPath), "not-a-protobuf-body");
  EXPECT_EQ(http::status::bad_request, malformed_submit_response.result());
  EXPECT_NE(parse_runtime_response(malformed_submit_response).error_message().find("Failed to parse protobuf request"),
            std::string::npos);

  const auto malformed_reorchestrate_response =
      send_http_request(port, http::verb::post, "/v1/workflows/runtime_demo:reorchestrate", "not-a-protobuf-body");
  EXPECT_EQ(http::status::bad_request, malformed_reorchestrate_response.result());

  auto mismatched_request = make_reorchestrate_request("other_workflow");
  const auto workflow_mismatch_response = send_http_request(
      port, http::verb::post, "/v1/workflows/runtime_demo:reorchestrate", serialize_proto(mismatched_request));
  EXPECT_EQ(http::status::bad_request, workflow_mismatch_response.result());
  EXPECT_NE(parse_runtime_response(workflow_mismatch_response).error_message().find("does not match"),
            std::string::npos);

  const auto empty_path_identifier_response = send_http_request(
      port, http::verb::post, "/v1/workflows/:reorchestrate", serialize_proto(make_reorchestrate_request("")));
  EXPECT_EQ(http::status::not_found, empty_path_identifier_response.result());
  EXPECT_NE(parse_runtime_response(empty_path_identifier_response).error_message().find("Unsupported route"),
            std::string::npos);

  const auto unsupported_route_response =
      send_http_request(port, http::verb::post, "/v1/unknown", serialize_proto(make_submit_request()));
  EXPECT_EQ(http::status::not_found, unsupported_route_response.result());

  server.stop();
}

TEST(RuntimeApiTest, HttpTransportSupportsApiKeyHeaderAuthAndLifecycleQueries) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::ApiKey, .expected_credential = "key-123", .require_secure_transport = false});
  tp::BeastHttpWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();
  ASSERT_TRUE(server.running());

  auto submit_request = make_submit_request();
  submit_request.clear_auth();
  const auto response = send_http_request(endpoint_port(server.endpoint()),
                                          http::verb::post,
                                          std::string(tp::kHttpSubmitWorkflowPath),
                                          serialize_proto(submit_request),
                                          "",
                                          "key-123");

  ASSERT_TRUE(parse_runtime_response(response).ok());

  server.stop();
  EXPECT_FALSE(server.running());
}

TEST(RuntimeApiTest, HttpTransportSurfacesTlsInitializationFailuresClearly) {
  to::app::InMemoryWorkflowRuntimeService service;

  auto failing_provider = std::make_shared<StubTlsCredentialProvider>();
  failing_provider->server_result.error_message = "server credentials failed";
  failing_provider->client_result.error_message = "client credentials failed";

  tp::BeastHttpWorkflowApiServer load_failure_server(
      service, {.port = 0, .use_tls = true, .tls = make_server_tls_config()}, failing_provider);
  load_failure_server.start();
  EXPECT_FALSE(load_failure_server.running());

  tp::TlsServerConfig invalid_server_tls = make_server_tls_config();
  invalid_server_tls.identity.private_key = tp::TlsDataSource::from_inline_pem("not-a-private-key");
  tp::BeastHttpWorkflowApiServer invalid_tls_server(service, {.port = 0, .use_tls = true, .tls = invalid_server_tls});
  invalid_tls_server.start();
  EXPECT_FALSE(invalid_tls_server.running());

  tp::BeastHttpWorkflowApiServer invalid_bind_server(
      service, {.bind_address = "not_an_ip_address", .port = 0, .use_tls = false, .tls = {}});
  invalid_bind_server.start();
  EXPECT_FALSE(invalid_bind_server.running());

  tp::BeastHttpWorkflowApiClient load_failure_client({.host = "localhost",
                                                      .port = 1,
                                                      .use_tls = true,
                                                      .tls = make_client_tls_config(),
                                                      .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                                      .bearer_token = "",
                                                      .api_key = ""},
                                                     failing_provider);
  const auto load_failure_response = load_failure_client.submit(make_submit_request());
  EXPECT_FALSE(load_failure_response.ok());
  EXPECT_NE(load_failure_response.error_message().find("client credentials failed"), std::string::npos);

  tp::TlsClientConfig invalid_client_tls = make_client_tls_config();
  invalid_client_tls.server_trust.root_certificates = tp::TlsDataSource::from_inline_pem("not-a-certificate");
  tp::BeastHttpWorkflowApiClient invalid_tls_client({.host = "localhost",
                                                     .port = 1,
                                                     .use_tls = true,
                                                     .tls = invalid_client_tls,
                                                     .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                                     .bearer_token = "",
                                                     .api_key = ""});
  const auto invalid_tls_response = invalid_tls_client.submit(make_submit_request());
  EXPECT_FALSE(invalid_tls_response.ok());
  EXPECT_NE(invalid_tls_response.error_message().find("HTTP transport failed"), std::string::npos);
}

TEST(RuntimeApiTest, HttpTlsTransportAllowsOptionalClientIdentityConfiguration) {
  to::app::InMemoryWorkflowRuntimeService service;
  tp::TlsServerConfig server_tls = make_server_tls_config();
  server_tls.identity.private_key_password = tp::TlsDataSource::from_inline_pem("unused-password");
  tp::BeastHttpWorkflowApiServer server(service, {.port = 0, .use_tls = true, .tls = server_tls});
  server.start();

  tp::TlsClientConfig client_tls = make_client_tls_config();
  client_tls.identity.certificate_chain = tp::TlsDataSource::from_inline_pem(test_tls_material().certificate_chain_pem);
  client_tls.identity.private_key = tp::TlsDataSource::from_inline_pem(test_tls_material().private_key_pem);
  client_tls.identity.private_key_password = tp::TlsDataSource::from_inline_pem("unused-password");
  tp::BeastHttpWorkflowApiClient client({.host = "localhost",
                                         .port = endpoint_port(server.endpoint()),
                                         .use_tls = true,
                                         .tls = client_tls,
                                         .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                         .bearer_token = "",
                                         .api_key = ""});

  const auto response = client.submit(make_submit_request());
  ASSERT_TRUE(response.ok()) << response.error_message();

  server.stop();
}

TEST(RuntimeApiTest, HttpTransportRoundTripsOverRealSocketAndSupportsHeaderAuth) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::BearerToken, .expected_credential = "token-123", .require_secure_transport = false});
  tp::BeastHttpWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();

  tp::BeastHttpWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                         .port = endpoint_port(server.endpoint()),
                                         .use_tls = false,
                                         .tls = {},
                                         .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                         .bearer_token = "token-123",
                                         .api_key = ""});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  const auto submit_response = client.submit(submit_request);
  ASSERT_TRUE(submit_response.ok()) << submit_response.error_message();
  EXPECT_EQ(2, submit_response.result().assignments_size());

  auto reorchestrate_request = make_reorchestrate_request("runtime_demo");
  pb::ActorStateOverride* actor_override = reorchestrate_request.add_actor_overrides();
  actor_override->set_actor_id("robot_1");
  actor_override->set_unavailable(true);
  const auto reorchestrate_response = client.reorchestrate(reorchestrate_request);
  EXPECT_TRUE(reorchestrate_response.ok());

  server.stop();
}

TEST(RuntimeApiTest, HttpTlsTransportRoundTripsAndMarksSecureTransport) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::BearerToken, .expected_credential = "token-123", .require_secure_transport = true});
  tp::BeastHttpWorkflowApiServer server(service, {.port = 0, .use_tls = true, .tls = make_server_tls_config()});
  server.start();

  tp::BeastHttpWorkflowApiClient client({.host = "localhost",
                                         .port = endpoint_port(server.endpoint()),
                                         .use_tls = true,
                                         .tls = make_client_tls_config(),
                                         .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                         .bearer_token = "token-123",
                                         .api_key = ""});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  const auto submit_response = client.submit(submit_request);
  ASSERT_TRUE(submit_response.ok()) << submit_response.error_message();
  EXPECT_EQ(2, submit_response.result().assignments_size());

  server.stop();
}

TEST(RuntimeApiTest, HttpClientRejectsMalformedProtobufResponsesFromPlainPeer) {
  const OneShotServerHandle server = start_one_shot_tcp_server([](tcp::socket socket) {
    receive_some_bytes(socket);
    static constexpr std::string_view kMalformedProtoResponse =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/x-protobuf\r\n"
        "Content-Length: 13\r\n"
        "Connection: close\r\n\r\n"
        "not-a-protobuf";
    beast::error_code error_code;
    asio::write(socket, asio::buffer(kMalformedProtoResponse), error_code);
    socket.shutdown(tcp::socket::shutdown_both, error_code);
  });

  tp::BeastHttpWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                         .port = server.port,
                                         .use_tls = false,
                                         .tls = {},
                                         .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                         .bearer_token = "",
                                         .api_key = ""});

  const auto response = client.submit(make_submit_request());
  EXPECT_FALSE(response.ok());
  EXPECT_NE(response.error_message().find("Failed to parse protobuf"), std::string::npos);
}

TEST(RuntimeApiTest, HttpClientReportsResolveAndReadFailuresFromPlainPeer) {
  tp::BeastHttpWorkflowApiClient unresolved_client({.host = "task-orchestrator-invalid-host.invalid",
                                                    .port = 80,
                                                    .use_tls = false,
                                                    .tls = {},
                                                    .timeout_ms = 200,
                                                    .bearer_token = "",
                                                    .api_key = ""});
  const auto unresolved_response = unresolved_client.submit(make_submit_request());
  EXPECT_FALSE(unresolved_response.ok());
  EXPECT_NE(unresolved_response.error_message().find("HTTP transport failed"), std::string::npos);

  const OneShotServerHandle closing_server = start_one_shot_tcp_server([](tcp::socket socket) {
    receive_some_bytes(socket);
    beast::error_code error_code;
    socket.shutdown(tcp::socket::shutdown_both, error_code);
    socket.close(error_code);
  });

  tp::BeastHttpWorkflowApiClient closing_client({.host = tp::kDefaultLoopbackAddress,
                                                 .port = closing_server.port,
                                                 .use_tls = false,
                                                 .tls = {},
                                                 .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                                 .bearer_token = "",
                                                 .api_key = ""});
  const auto closing_response = closing_client.submit(make_submit_request());
  EXPECT_FALSE(closing_response.ok());
  EXPECT_NE(closing_response.error_message().find("HTTP transport failed"), std::string::npos);
}

TEST(RuntimeApiTest, HttpClientsReportConnectFailuresAgainstUnusedPorts) {
  const int unused_port = reserve_unused_port();

  tp::BeastHttpWorkflowApiClient plain_client({.host = tp::kDefaultLoopbackAddress,
                                               .port = unused_port,
                                               .use_tls = false,
                                               .tls = {},
                                               .timeout_ms = 200,
                                               .bearer_token = "",
                                               .api_key = ""});
  const auto plain_response = plain_client.submit(make_submit_request());
  EXPECT_FALSE(plain_response.ok());
  EXPECT_NE(std::string::npos, plain_response.error_message().find("HTTP transport failed"));

  tp::BeastHttpWorkflowApiClient tls_client({.host = tp::kDefaultLoopbackAddress,
                                             .port = unused_port,
                                             .use_tls = true,
                                             .tls = make_client_tls_config(tp::kDefaultLoopbackAddress),
                                             .timeout_ms = 200,
                                             .bearer_token = "",
                                             .api_key = ""});
  const auto tls_response = tls_client.submit(make_submit_request());
  EXPECT_FALSE(tls_response.ok());
  EXPECT_NE(std::string::npos, tls_response.error_message().find("HTTP transport failed"));
}

TEST(RuntimeApiTest, HttpTlsClientReportsResolveHandshakeAndReadFailures) {
  tp::BeastHttpWorkflowApiClient unresolved_client({.host = "task-orchestrator-invalid-host.invalid",
                                                    .port = 443,
                                                    .use_tls = true,
                                                    .tls = make_client_tls_config(),
                                                    .timeout_ms = 200,
                                                    .bearer_token = "",
                                                    .api_key = ""});
  const auto unresolved_response = unresolved_client.submit(make_submit_request());
  EXPECT_FALSE(unresolved_response.ok());
  EXPECT_NE(unresolved_response.error_message().find("HTTP transport failed"), std::string::npos);

  const OneShotServerHandle plain_server = start_one_shot_tcp_server([](tcp::socket socket) {
    std::this_thread::sleep_for(kTransportFailurePropagationDelay);
    beast::error_code error_code;
    socket.shutdown(tcp::socket::shutdown_both, error_code);
    socket.close(error_code);
  });

  tp::BeastHttpWorkflowApiClient handshake_client({.host = tp::kDefaultLoopbackAddress,
                                                   .port = plain_server.port,
                                                   .use_tls = true,
                                                   .tls = make_client_tls_config(tp::kDefaultLoopbackAddress),
                                                   .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                                   .bearer_token = "",
                                                   .api_key = ""});
  const auto handshake_response = handshake_client.submit(make_submit_request());
  EXPECT_FALSE(handshake_response.ok());
  EXPECT_NE(handshake_response.error_message().find("HTTP transport failed"), std::string::npos);

  const OneShotServerHandle closing_tls_server = start_one_shot_tls_server([](TestTlsServerStream& stream) {
    receive_some_bytes(stream);
    beast::error_code error_code;
    stream.next_layer().shutdown(tcp::socket::shutdown_both, error_code);
    stream.next_layer().close(error_code);
  });

  tp::BeastHttpWorkflowApiClient closing_tls_client({.host = tp::kDefaultLoopbackAddress,
                                                     .port = closing_tls_server.port,
                                                     .use_tls = true,
                                                     .tls = make_client_tls_config(tp::kDefaultLoopbackAddress),
                                                     .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                                     .bearer_token = "",
                                                     .api_key = ""});
  const auto closing_response = closing_tls_client.submit(make_submit_request());
  EXPECT_FALSE(closing_response.ok());
  EXPECT_NE(closing_response.error_message().find("HTTP transport failed"), std::string::npos);
}

TEST(RuntimeApiTest, HttpTlsClientRejectsInvalidPeerNameBeforeHandshake) {
  const OneShotServerHandle plain_server = start_one_shot_tcp_server([](tcp::socket socket) {
    std::this_thread::sleep_for(kTransportFailurePropagationDelay);
    beast::error_code error_code;
    socket.shutdown(tcp::socket::shutdown_both, error_code);
    socket.close(error_code);
  });

  tp::BeastHttpWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                         .port = plain_server.port,
                                         .use_tls = true,
                                         .tls = make_client_tls_config(std::string(300U, 'a')),
                                         .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                         .bearer_token = "",
                                         .api_key = ""});
  const auto response = client.submit(make_submit_request());
  EXPECT_FALSE(response.ok());
  EXPECT_NE(std::string::npos, response.error_message().find("server name indication"));
}

TEST(RuntimeApiTest, HttpServerHandlesAbruptPlainAndTlsDisconnectsGracefully) {
  to::app::InMemoryWorkflowRuntimeService service;

  tp::BeastHttpWorkflowApiServer plain_server(service, {.port = 0, .tls = {}});
  plain_server.start();
  ASSERT_TRUE(plain_server.running());
  {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    beast::error_code error_code;
    const auto endpoints = resolver.resolve(
        tp::kDefaultLoopbackAddress, std::to_string(endpoint_port(plain_server.endpoint())), error_code);
    ASSERT_FALSE(error_code);
    asio::connect(socket, endpoints, error_code);
    ASSERT_FALSE(error_code);
    socket.close(error_code);
  }
  std::this_thread::sleep_for(kTransportFailurePropagationDelay);
  EXPECT_TRUE(plain_server.running());
  plain_server.stop();

  tp::BeastHttpWorkflowApiServer tls_server(service, {.port = 0, .use_tls = true, .tls = make_server_tls_config()});
  tls_server.start();
  ASSERT_TRUE(tls_server.running());
  {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    beast::error_code error_code;
    const auto endpoints =
        resolver.resolve("localhost", std::to_string(endpoint_port(tls_server.endpoint())), error_code);
    ASSERT_FALSE(error_code);
    asio::connect(socket, endpoints, error_code);
    ASSERT_FALSE(error_code);
    socket.close(error_code);
  }
  std::this_thread::sleep_for(kTransportFailurePropagationDelay);
  EXPECT_TRUE(tls_server.running());
  tls_server.stop();
}

TEST(RuntimeApiTest, HttpServerSurvivesClientResetDuringResponseWrite) {
  to::app::InMemoryWorkflowRuntimeService service;
  tp::BeastHttpWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();
  ASSERT_TRUE(server.running());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  beast::error_code error_code;
  const auto endpoints =
      resolver.resolve(tp::kDefaultLoopbackAddress, std::to_string(endpoint_port(server.endpoint())), error_code);
  ASSERT_FALSE(error_code);
  asio::connect(socket, endpoints, error_code);
  ASSERT_FALSE(error_code);

  http::request<http::string_body> request(http::verb::post, std::string(tp::kHttpSubmitWorkflowPath), 11);
  request.set(http::field::host, tp::kDefaultLoopbackAddress);
  request.set(http::field::content_type, std::string(tp::kBinaryProtoContentType));
  request.body() = serialize_proto(make_submit_request());
  request.prepare_payload();
  http::write(socket, request, error_code);
  ASSERT_FALSE(error_code);
  close_with_tcp_reset(socket);

  std::this_thread::sleep_for(kTransportFailurePropagationDelay);
  EXPECT_TRUE(server.running());
  server.stop();
}

// NOLINTEND(bugprone-unused-return-value)

TEST(RuntimeApiTest, GrpcTransportRoundTripsOverRealSocketAndSupportsMetadataAuth) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::ApiKey, .expected_credential = "key-123", .require_secure_transport = false});
  tp::GrpcWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();

  tp::GrpcWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                    .port = endpoint_port(server.endpoint()),
                                    .use_tls = false,
                                    .tls = {},
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "",
                                    .api_key = "key-123"});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  const auto submit_response = client.submit_async(submit_request).get();
  ASSERT_TRUE(submit_response.ok()) << submit_response.error_message();
  EXPECT_EQ(2, submit_response.result().assignments_size());

  auto reorchestrate_request = make_reorchestrate_request("runtime_demo");
  pb::ActorStateOverride* actor_override = reorchestrate_request.add_actor_overrides();
  actor_override->set_actor_id("robot_1");
  actor_override->set_unavailable(true);
  const auto reorchestrate_response = client.reorchestrate_async(reorchestrate_request).get();
  EXPECT_TRUE(reorchestrate_response.ok());

  server.stop();
}

TEST(RuntimeApiTest, GrpcStreamingSubmissionExposesWorkflowEventsToClients) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::ApiKey, .expected_credential = "key-123", .require_secure_transport = false});
  tp::GrpcWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();

  tp::GrpcWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                    .port = endpoint_port(server.endpoint()),
                                    .use_tls = false,
                                    .tls = {},
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "",
                                    .api_key = "key-123"});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  const std::vector<tp::WorkflowEvent> events = collect_events(client.submit_stream(std::move(submit_request)));

  ASSERT_GE(events.size(), 4U);
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, events.front().type());
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED, events[1].type());
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, events.back().type());
  ASSERT_TRUE(events.back().has_response());
  EXPECT_TRUE(events.back().response().ok());
  EXPECT_EQ(2, events.back().response().result().assignments_size());
  EXPECT_EQ(2, std::count_if(events.begin(), events.end(), [](const tp::WorkflowEvent& event) {
              return event.type() == pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED;
            }));

  server.stop();
}

TEST(RuntimeApiTest, GrpcStreamingReorchestrateExposesOverrideAndCompletionEventsToClients) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::ApiKey, .expected_credential = "key-123", .require_secure_transport = false});
  tp::GrpcWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();

  tp::GrpcWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                    .port = endpoint_port(server.endpoint()),
                                    .use_tls = false,
                                    .tls = {},
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "",
                                    .api_key = "key-123"});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  ASSERT_TRUE(client.submit_async(submit_request).get().ok());

  tp::ReorchestrateRequest request = make_reorchestrate_request("runtime_demo");
  request.clear_auth();
  pb::TaskStateOverride* completed_task = request.add_task_overrides();
  completed_task->set_task_id("pick");
  completed_task->set_completed(true);
  std::vector<tp::WorkflowEvent> events = collect_events(client.reorchestrate_stream(std::move(request)));

  ASSERT_GE(events.size(), 3U);
  EXPECT_TRUE(std::ranges::any_of(events, [](const tp::WorkflowEvent& event) {
    return event.type() == pb::WORKFLOW_EVENT_TYPE_TASK_COMPLETED && event.task_id() == "pick";
  }));
  EXPECT_TRUE(std::ranges::any_of(events, [](const tp::WorkflowEvent& event) {
    return event.type() == pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED;
  }));
  ASSERT_EQ(pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, events.back().type());
  ASSERT_TRUE(events.back().has_response());
  EXPECT_TRUE(events.back().response().ok());

  server.stop();
}

TEST(RuntimeApiTest, GrpcStreamingClientCancellationDoesNotBreakLaterRequests) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::ApiKey, .expected_credential = "key-123", .require_secure_transport = false});
  tp::GrpcWorkflowApiServer server(service,
                                   {.port = 0,
                                    .tls = {},
                                    .completion_queue_threads = 0,
                                    .max_receive_message_bytes = static_cast<int>(tp::kDefaultMaxHttpBodyBytes),
                                    .max_send_message_bytes = static_cast<int>(tp::kDefaultMaxHttpBodyBytes)});
  server.start();

  tp::GrpcWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                    .port = endpoint_port(server.endpoint()),
                                    .use_tls = false,
                                    .tls = {},
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "",
                                    .api_key = "key-123"});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  {
    auto stream = client.submit_stream(std::move(submit_request));
    auto iterator = stream.begin();
    ASSERT_TRUE(iterator != stream.end());
    EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, (*iterator).type());
  }

  std::this_thread::sleep_for(kTransportFailurePropagationDelay);
  tp::SubmitWorkflowRequest second_request = make_submit_request();
  second_request.clear_auth();
  second_request.mutable_config()->set_id("runtime_demo_after_cancel");
  const auto response = client.submit_async(second_request).get();
  ASSERT_TRUE(response.ok()) << response.error_message();
  EXPECT_EQ(2, response.result().assignments_size());

  server.stop();
}

TEST(RuntimeApiTest, GrpcStreamingClientSurfacesTransportFailuresClearly) {
  to::app::InMemoryWorkflowRuntimeService service;
  tp::GrpcWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();
  const int grpc_port = endpoint_port(server.endpoint());
  server.stop();

  tp::GrpcWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                    .port = grpc_port,
                                    .use_tls = false,
                                    .tls = {},
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "",
                                    .api_key = ""});

  std::vector<tp::WorkflowEvent> events = collect_events(client.submit_stream(make_submit_request()));
  ASSERT_EQ(1U, events.size());
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, events.front().type());
  ASSERT_TRUE(events.front().has_response());
  EXPECT_FALSE(events.front().response().ok());
  EXPECT_NE(events.front().response().error_message().find("gRPC transport failed"), std::string::npos);
}

TEST(RuntimeApiTest, GrpcTlsTransportRoundTripsAndMarksSecureTransport) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::ApiKey, .expected_credential = "key-123", .require_secure_transport = true});
  tp::GrpcWorkflowApiServer server(service, {.port = 0, .use_tls = true, .tls = make_server_tls_config()});
  server.start();

  tp::GrpcWorkflowApiClient client({.host = "localhost",
                                    .port = endpoint_port(server.endpoint()),
                                    .use_tls = true,
                                    .tls = make_client_tls_config(),
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "",
                                    .api_key = "key-123"});

  tp::SubmitWorkflowRequest submit_request = make_submit_request();
  submit_request.clear_auth();
  const auto submit_response = client.submit_async(submit_request).get();
  ASSERT_TRUE(submit_response.ok()) << submit_response.error_message();
  EXPECT_EQ(2, submit_response.result().assignments_size());

  server.stop();
}

TEST(RuntimeApiTest, GrpcTransportSupportsBearerMetadataAndLifecycleQueries) {
  to::app::InMemoryWorkflowRuntimeService service(
      {.mode = tp::AuthMode::BearerToken, .expected_credential = "token-123", .require_secure_transport = false});
  tp::GrpcWorkflowApiServer server(service, {.port = 0, .tls = {}});
  server.start();
  ASSERT_TRUE(server.running());

  tp::GrpcWorkflowApiClient client({.host = tp::kDefaultLoopbackAddress,
                                    .port = endpoint_port(server.endpoint()),
                                    .use_tls = false,
                                    .tls = {},
                                    .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                    .bearer_token = "token-123",
                                    .api_key = ""});

  auto request = make_submit_request();
  request.clear_auth();
  const auto response = client.submit_async(request).get();
  ASSERT_TRUE(response.ok()) << response.error_message();
  EXPECT_EQ(2, response.result().assignments_size());

  server.stop();
  EXPECT_FALSE(server.running());
}

TEST(RuntimeApiTest, GrpcTransportSurfacesTlsAndStartupFailuresClearly) {
  to::app::InMemoryWorkflowRuntimeService service;

  auto failing_provider = std::make_shared<StubTlsCredentialProvider>();
  failing_provider->server_result.error_message = "server credentials failed";
  failing_provider->client_result.error_message = "client credentials failed";

  tp::GrpcWorkflowApiServer load_failure_server(
      service, {.port = 0, .use_tls = true, .tls = make_server_tls_config()}, failing_provider);
  load_failure_server.start();
  EXPECT_FALSE(load_failure_server.running());

  tp::TlsServerConfig invalid_server_tls = make_server_tls_config();
  invalid_server_tls.require_client_certificate = true;
  invalid_server_tls.client_trust.verify_peer = false;
  tp::GrpcWorkflowApiServer invalid_tls_server(service, {.port = 0, .use_tls = true, .tls = invalid_server_tls});
  invalid_tls_server.start();
  EXPECT_FALSE(invalid_tls_server.running());

  tp::GrpcWorkflowApiServer invalid_bind_server(
      service, {.bind_address = "not_an_ip_address", .port = 0, .use_tls = false, .tls = {}});
  invalid_bind_server.start();
  EXPECT_FALSE(invalid_bind_server.running());

  tp::GrpcWorkflowApiClient load_failure_client({.host = "localhost",
                                                 .port = 1,
                                                 .use_tls = true,
                                                 .tls = make_client_tls_config(),
                                                 .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                                 .bearer_token = "",
                                                 .api_key = ""},
                                                failing_provider);
  const auto load_failure_response = load_failure_client.submit_async(make_submit_request()).get();
  EXPECT_FALSE(load_failure_response.ok());
  EXPECT_NE(load_failure_response.error_message().find("client credentials failed"), std::string::npos);

  tp::TlsClientConfig invalid_client_tls = make_client_tls_config();
  invalid_client_tls.server_trust.verify_peer = false;
  tp::GrpcWorkflowApiClient invalid_tls_client({.host = "localhost",
                                                .port = 1,
                                                .use_tls = true,
                                                .tls = invalid_client_tls,
                                                .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                                .bearer_token = "",
                                                .api_key = ""});
  const auto invalid_tls_response = invalid_tls_client.submit_async(make_submit_request()).get();
  EXPECT_FALSE(invalid_tls_response.ok());
  EXPECT_NE(invalid_tls_response.error_message().find("peer verification"), std::string::npos);
}

TEST(RuntimeApiTest, AsyncSubmissionReturnsDeterministicPlan) {
  to::app::InMemoryWorkflowRuntimeService service;
  auto future = service.submit_workflow_async(make_submit_request());

  const auto response = future.get();
  ASSERT_TRUE(response.ok());
  ASSERT_EQ(2, response.result().assignments_size());
  EXPECT_EQ("pick", response.result().assignments(0).task_id());
  EXPECT_EQ("pack", response.result().assignments(1).task_id());
}

TEST(RuntimeApiTest, NetworkClientsSurfaceTransportFailuresClearly) {
  to::app::InMemoryWorkflowRuntimeService service;

  tp::BeastHttpWorkflowApiServer http_server(service, {.port = 0, .tls = {}});
  http_server.start();
  const int http_port = endpoint_port(http_server.endpoint());
  http_server.stop();

  tp::GrpcWorkflowApiServer grpc_server(service, {.port = 0, .tls = {}});
  grpc_server.start();
  const int grpc_port = endpoint_port(grpc_server.endpoint());
  grpc_server.stop();

  tp::BeastHttpWorkflowApiClient http_client({.host = tp::kDefaultLoopbackAddress,
                                              .port = http_port,
                                              .use_tls = false,
                                              .tls = {},
                                              .timeout_ms = tp::kDefaultRequestTimeoutMs,
                                              .bearer_token = "",
                                              .api_key = ""});
  tp::GrpcWorkflowApiClient grpc_client({.host = tp::kDefaultLoopbackAddress,
                                         .port = grpc_port,
                                         .use_tls = false,
                                         .tls = {},
                                         .deadline_ms = tp::kDefaultRequestTimeoutMs,
                                         .bearer_token = "",
                                         .api_key = ""});

  const auto http_response = http_client.submit(make_submit_request());
  EXPECT_FALSE(http_response.ok());
  EXPECT_NE(std::string::npos, http_response.error_message().find("HTTP transport failed"));

  const auto grpc_response = grpc_client.submit_async(make_submit_request()).get();
  EXPECT_FALSE(grpc_response.ok());
  EXPECT_NE(std::string::npos, grpc_response.error_message().find("gRPC transport failed"));
}

}  // namespace
