#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <future>
#include <memory>
#include <string>
#include <string_view>

#include "src/detail/http_transport_detail.hpp"
#include "test_support_tls_material.hpp"

namespace task_orchestrator::protocol {
namespace {
namespace tp = task_orchestrator::protocol;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;

const tp::test_support::TestTlsMaterial& test_tls_material() noexcept {
  return tp::test_support::localhost_tls_material();
}

template <typename Result>
std::future<Result> make_ready_future(Result value) {
  std::promise<Result> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

RuntimeApiResponse make_ok_response(std::string message) {
  RuntimeApiResponse response;
  response.set_ok(true);
  response.set_error_message(std::move(message));
  response.mutable_result()->set_ok(true);
  return response;
}

class CapturingWorkflowRuntimeService final : public WorkflowRuntimeService {
 public:
  RuntimeApiResponse submit_workflow(const SubmitWorkflowRequest& request) override {
    last_submit = request;
    ++submit_calls;
    return submit_response;
  }

  RuntimeApiResponse reorchestrate(const ReorchestrateRequest& request) override {
    last_reorchestrate = request;
    ++reorchestrate_calls;
    return reorchestrate_response;
  }

  std::future<RuntimeApiResponse> submit_workflow_async(const SubmitWorkflowRequest& request) override {
    return make_ready_future(submit_workflow(request));
  }

  std::future<RuntimeApiResponse> reorchestrate_async(const ReorchestrateRequest& request) override {
    return make_ready_future(reorchestrate(request));
  }

  WorkflowEventStream stream_submit_workflow(SubmitWorkflowRequest) override { co_return; }

  WorkflowEventStream stream_reorchestrate(ReorchestrateRequest) override { co_return; }

  SubmitWorkflowRequest last_submit;
  ReorchestrateRequest last_reorchestrate;
  RuntimeApiResponse submit_response = make_ok_response("submit ok");
  RuntimeApiResponse reorchestrate_response = make_ok_response("replan ok");
  int submit_calls = 0;
  int reorchestrate_calls = 0;
};

class FailingTlsCredentialProvider final : public TlsCredentialProvider {
 public:
  TlsServerLoadResult load_server_credentials(const TlsServerConfig&) const override {
    return TlsServerLoadResult{
        .value = {},
        .error_message = "server tls unavailable",
    };
  }

  TlsClientLoadResult load_client_credentials(const TlsClientConfig&) const override {
    return TlsClientLoadResult{
        .value = {},
        .error_message = "client tls unavailable",
    };
  }
};

struct FailingSerializableMessage {
  static bool SerializeToString(std::string*) { return false; }  // NOLINT(readability-identifier-naming)
};

int port_from_endpoint(const std::string& endpoint) {
  return std::stoi(endpoint.substr(endpoint.find_last_of(':') + 1));
}

TEST(HttpTransportInternalTest, ProtobufHelpersAndRouteExtractionAreDeterministic) {
  tp::SubmitWorkflowRequest request;
  request.mutable_config()->set_id("workflow_demo");

  std::string body;
  std::string content_type;
  std::string error_message;
  EXPECT_TRUE(detail::serialize_message(request, &body, &content_type, &error_message));
  EXPECT_EQ(std::string(tp::kBinaryProtoContentType), content_type);
  EXPECT_TRUE(error_message.empty());

  tp::SubmitWorkflowRequest parsed_request;
  std::string parse_error;
  EXPECT_TRUE(detail::parse_message(body, &parsed_request, &parse_error));
  EXPECT_EQ("workflow_demo", parsed_request.config().id());

  const std::string invalid_payload(1, static_cast<char>(0xFF));
  EXPECT_FALSE(detail::parse_message(invalid_payload, &parsed_request, &parse_error));
  EXPECT_NE(parse_error.find("Failed to parse protobuf request body"), std::string::npos);

  const auto workflow_id = detail::extract_workflow_id_from_target("/v1/workflows/demo:reorchestrate");
  ASSERT_TRUE(workflow_id.has_value());
  EXPECT_EQ("demo", *workflow_id);
  EXPECT_FALSE(detail::extract_workflow_id_from_target("/v1/workflows/:reorchestrate").has_value());
  EXPECT_FALSE(detail::extract_workflow_id_from_target("/v1/other").has_value());

  const auto error_response = detail::make_transport_error_response("transport failed");
  EXPECT_FALSE(error_response.ok());
  EXPECT_EQ("transport failed", error_response.error_message());
  EXPECT_EQ("transport failed", error_response.result().error_message());
}

TEST(HttpTransportInternalTest, TlsHelperFunctionsValidateSuccessAndFailureCases) {
  ASSERT_TRUE(test_tls_material().ok) << test_tls_material().error_message;
  ssl::context server_context(ssl::context::tls_server);
  ssl::context client_context(ssl::context::tls_client);
  std::string error_message;

  EXPECT_TRUE(detail::configure_minimum_tls_version(server_context, &error_message));

  const tp::LoadedTlsIdentityConfig valid_identity{
      .certificate_chain_pem = test_tls_material().certificate_chain_pem,
      .private_key_pem = test_tls_material().private_key_pem,
      .private_key_password = {},
  };
  EXPECT_TRUE(detail::configure_tls_identity(server_context, valid_identity, "HTTP server", &error_message));

  const tp::LoadedTlsIdentityConfig invalid_identity{
      .certificate_chain_pem = "not-a-certificate",
      .private_key_pem = "not-a-private-key",
      .private_key_password = {},
  };
  EXPECT_FALSE(detail::configure_tls_identity(server_context, invalid_identity, "HTTP server", &error_message));
  EXPECT_FALSE(error_message.empty());

  const tp::LoadedTlsTrustConfig valid_trust{
      .root_certificates_pem = test_tls_material().certificate_chain_pem,
      .use_system_default_roots = false,
      .verify_peer = true,
      .expected_peer_name = "localhost",
  };
  EXPECT_TRUE(detail::configure_tls_trust(client_context, valid_trust, "HTTP client", &error_message));

  const tp::LoadedTlsTrustConfig invalid_trust{
      .root_certificates_pem = "not-a-root",
      .use_system_default_roots = false,
      .verify_peer = true,
      .expected_peer_name = "localhost",
  };
  EXPECT_FALSE(detail::configure_tls_trust(client_context, invalid_trust, "HTTP client", &error_message));
  EXPECT_FALSE(error_message.empty());

  std::shared_ptr<ssl::context> built_server_context;
  EXPECT_TRUE(detail::make_server_tls_context(
      tp::LoadedTlsServerConfig{
          .identity = valid_identity,
          .client_trust =
              {
                  .root_certificates_pem = test_tls_material().certificate_chain_pem,
                  .use_system_default_roots = false,
                  .verify_peer = true,
                  .expected_peer_name = {},
              },
          .require_client_certificate = true,
      },
      &built_server_context,
      &error_message));
  ASSERT_NE(nullptr, built_server_context);

  EXPECT_FALSE(detail::make_server_tls_context(
      tp::LoadedTlsServerConfig{
          .identity = invalid_identity,
          .client_trust = {},
          .require_client_certificate = false,
      },
      &built_server_context,
      &error_message));
  EXPECT_EQ(nullptr, built_server_context);

  std::shared_ptr<ssl::context> built_client_context;
  EXPECT_TRUE(detail::make_client_tls_context(
      tp::LoadedTlsClientConfig{
          .identity = valid_identity,
          .server_trust = valid_trust,
      },
      &built_client_context,
      &error_message));
  ASSERT_NE(nullptr, built_client_context);

  EXPECT_FALSE(detail::make_client_tls_context(
      tp::LoadedTlsClientConfig{
          .identity = {},
          .server_trust = invalid_trust,
      },
      &built_client_context,
      &error_message));
  EXPECT_EQ(nullptr, built_client_context);

  EXPECT_FALSE(detail::make_client_tls_context(
      tp::LoadedTlsClientConfig{
          .identity = invalid_identity,
          .server_trust = valid_trust,
      },
      &built_client_context,
      &error_message));
  EXPECT_EQ(nullptr, built_client_context);
}

TEST(HttpTransportInternalTest, PeerNameAndHttpErrorHelpersAreDeterministic) {
  const tp::HttpClientOptions options{
      .host = "service.internal",
      .port = 443,
      .use_tls = true,
      .tls = {},
      .timeout_ms = tp::kDefaultRequestTimeoutMs,
      .bearer_token = {},
      .api_key = {},
  };
  const tp::LoadedTlsClientConfig default_peer_config{
      .identity = {},
      .server_trust =
          {
              .root_certificates_pem = {},
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = {},
          },
  };
  const tp::LoadedTlsClientConfig override_peer_config{
      .identity = {},
      .server_trust =
          {
              .root_certificates_pem = {},
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = "override.internal",
          },
  };
  EXPECT_EQ("service.internal", detail::resolve_expected_peer_name(options, default_peer_config));
  EXPECT_EQ("override.internal", detail::resolve_expected_peer_name(options, override_peer_config));

  asio::io_context io_context;
  ssl::context tls_context(ssl::context::tls_client);
  detail::TlsClientStream stream(io_context, tls_context);
  std::string error_message;
  EXPECT_TRUE(detail::configure_tls_server_name(stream, "", &error_message));
  EXPECT_TRUE(detail::configure_tls_server_name(stream, "localhost", &error_message));
  EXPECT_FALSE(detail::configure_tls_server_name(stream, std::string(300U, 'a'), &error_message));
  EXPECT_NE(std::string::npos, error_message.find("server name indication"));

  EXPECT_TRUE(detail::assign_error_from_code({}, "context", &error_message));
  EXPECT_TRUE(detail::ignorable_tls_shutdown_error(asio::error::eof));
  EXPECT_TRUE(detail::ignorable_tls_shutdown_error(ssl::error::stream_truncated));
  EXPECT_FALSE(detail::ignorable_tls_shutdown_error(beast::error_code{}));

  const auto http_error = detail::make_http_error(http::status::bad_request, "bad request", 11, false);
  EXPECT_EQ(http::status::bad_request, http_error.result());

  tp::RuntimeApiResponse parsed_error;
  ASSERT_TRUE(parsed_error.ParseFromString(http_error.body()));
  EXPECT_FALSE(parsed_error.ok());
  EXPECT_EQ("bad request", parsed_error.error_message());
}

TEST(HttpTransportInternalTest, TlsPeerNameAndTrustHelpersSupportFallbackAndSystemRoots) {
  const tp::HttpClientOptions options{
      .host = "fallback.internal",
      .port = 443,
      .use_tls = true,
      .tls = {},
      .timeout_ms = tp::kDefaultRequestTimeoutMs,
      .bearer_token = {},
      .api_key = {},
  };
  const tp::LoadedTlsClientConfig tls_config{
      .identity = {},
      .server_trust =
          {
              .root_certificates_pem = {},
              .use_system_default_roots = true,
              .verify_peer = true,
              .expected_peer_name = {},
          },
  };

  EXPECT_EQ("fallback.internal", detail::resolve_expected_peer_name(options, tls_config));

  ssl::context context(ssl::context::tls_client);
  std::string error_message;
  EXPECT_TRUE(detail::configure_tls_trust(context, tls_config.server_trust, "HTTP client", &error_message));

  asio::io_context io_context;
  ssl::context stream_context(ssl::context::tls_client);
  detail::TlsClientStream stream(io_context, stream_context);
  EXPECT_TRUE(detail::configure_tls_server_name(stream, "", &error_message));
}

TEST(HttpTransportInternalTest, ApplyTransportAuthAndHandleRequestPopulateWorkflowRequests) {
  CapturingWorkflowRuntimeService runtime_service;

  tp::SubmitWorkflowRequest submit_request;
  submit_request.mutable_config()->set_id("submit_demo");
  std::string submit_body;
  std::string content_type;
  std::string error_message;
  ASSERT_TRUE(detail::serialize_message(submit_request, &submit_body, &content_type, &error_message));

  http::request<http::string_body> http_submit_request(http::verb::post, std::string(tp::kHttpSubmitWorkflowPath), 11);
  http_submit_request.keep_alive(true);
  http_submit_request.set(tp::kAuthorizationHeader, "Bearer token-123");
  http_submit_request.set(tp::kApiKeyHeader, "api-key-123");
  http_submit_request.body() = submit_body;
  http_submit_request.prepare_payload();

  const auto submit_response = detail::handle_http_request(runtime_service, false, http_submit_request);
  EXPECT_EQ(http::status::ok, submit_response.result());
  EXPECT_EQ(1, runtime_service.submit_calls);
  EXPECT_EQ("submit_demo", runtime_service.last_submit.config().id());
  EXPECT_EQ("token-123", runtime_service.last_submit.auth().bearer_token());
  EXPECT_EQ("api-key-123", runtime_service.last_submit.auth().api_key());
  EXPECT_FALSE(runtime_service.last_submit.auth().secure_transport());

  tp::ReorchestrateRequest mismatched_request;
  mismatched_request.set_workflow_id("body_id");
  std::string replan_body;
  ASSERT_TRUE(detail::serialize_message(mismatched_request, &replan_body, &content_type, &error_message));

  http::request<http::string_body> mismatch_http_request(http::verb::post, "/v1/workflows/path_id:reorchestrate", 11);
  mismatch_http_request.body() = replan_body;
  mismatch_http_request.prepare_payload();
  EXPECT_EQ(http::status::bad_request,
            detail::handle_http_request(runtime_service, true, mismatch_http_request).result());

  tp::ReorchestrateRequest reorchestrate_request;
  ASSERT_TRUE(detail::serialize_message(reorchestrate_request, &replan_body, &content_type, &error_message));

  http::request<http::string_body> good_http_request(http::verb::post, "/v1/workflows/path_id:reorchestrate", 11);
  good_http_request.set(tp::kApiKeyHeader, "secure-api-key");
  good_http_request.body() = replan_body;
  good_http_request.prepare_payload();

  const auto reorchestrate_response = detail::handle_http_request(runtime_service, true, good_http_request);
  EXPECT_EQ(http::status::ok, reorchestrate_response.result());
  EXPECT_EQ(1, runtime_service.reorchestrate_calls);
  EXPECT_EQ("path_id", runtime_service.last_reorchestrate.workflow_id());
  EXPECT_TRUE(runtime_service.last_reorchestrate.auth().secure_transport());
  EXPECT_EQ("secure-api-key", runtime_service.last_reorchestrate.auth().api_key());

  http::request<http::string_body> wrong_method(http::verb::get, std::string(tp::kHttpSubmitWorkflowPath), 11);
  EXPECT_EQ(http::status::method_not_allowed,
            detail::handle_http_request(runtime_service, false, wrong_method).result());

  http::request<http::string_body> unknown_route(http::verb::post, "/v1/unsupported", 11);
  unknown_route.prepare_payload();
  EXPECT_EQ(http::status::not_found, detail::handle_http_request(runtime_service, false, unknown_route).result());
}

TEST(HttpTransportInternalTest, ClientHelpersCoverSerializationRequestParsingAndStartupFailures) {
  std::string body;
  std::string content_type;
  std::string error_message;
  EXPECT_FALSE(detail::serialize_message(FailingSerializableMessage{}, &body, &content_type, &error_message));
  EXPECT_EQ("Failed to serialize protobuf message.", error_message);

  const tp::HttpClientOptions options{
      .host = "client.internal",
      .port = 8181,
      .use_tls = false,
      .tls = {},
      .timeout_ms = tp::kDefaultRequestTimeoutMs,
      .bearer_token = "bearer-token",
      .api_key = "api-key",
  };
  const auto request =
      detail::make_http_client_request(options, "/v1/workflows:submit", "payload", tp::kBinaryProtoContentType);
  EXPECT_EQ("client.internal", request[http::field::host]);
  EXPECT_EQ("Bearer bearer-token", request[tp::kAuthorizationHeader]);
  EXPECT_EQ("api-key", request[tp::kApiKeyHeader]);
  EXPECT_EQ("payload", request.body());

  http::response<http::string_body> invalid_response(http::status::ok, 11);
  invalid_response.body() = "not-a-protobuf-payload";
  invalid_response.prepare_payload();
  const auto parsed_response = detail::parse_http_runtime_response(invalid_response);
  EXPECT_FALSE(parsed_response.ok());
  EXPECT_NE(parsed_response.error_message().find("Failed to parse protobuf request body"), std::string::npos);

  auto failing_provider = std::make_shared<FailingTlsCredentialProvider>();
  BeastHttpWorkflowApiClient failing_client(
      tp::HttpClientOptions{
          .host = "localhost",
          .port = 443,
          .use_tls = true,
          .tls = {},
          .timeout_ms = tp::kDefaultRequestTimeoutMs,
          .bearer_token = {},
          .api_key = {},
      },
      failing_provider);
  tp::SubmitWorkflowRequest submit_request;
  const auto startup_failure = failing_client.submit(submit_request);
  EXPECT_FALSE(startup_failure.ok());
  EXPECT_NE(startup_failure.error_message().find("HTTP transport failed"), std::string::npos);
}

TEST(HttpTransportInternalTest, ServerStartupReportsTlsLoadInvalidAddressAndBindConflicts) {
  CapturingWorkflowRuntimeService runtime_service;
  auto failing_provider = std::make_shared<FailingTlsCredentialProvider>();

  BeastHttpWorkflowApiServer tls_server(runtime_service,
                                        tp::HttpEndpointOptions{
                                            .bind_address = tp::kDefaultLoopbackAddress,
                                            .port = 0,
                                            .use_tls = true,
                                            .tls = {},
                                            .io_threads = tp::kDefaultIoThreads,
                                            .max_body_bytes = tp::kDefaultMaxHttpBodyBytes,
                                        },
                                        failing_provider);
  tls_server.start();
  EXPECT_FALSE(tls_server.running());

  BeastHttpWorkflowApiServer bad_address_server(runtime_service,
                                                tp::HttpEndpointOptions{
                                                    .bind_address = "not-an-address",
                                                    .port = 0,
                                                    .use_tls = false,
                                                    .tls = {},
                                                    .io_threads = tp::kDefaultIoThreads,
                                                    .max_body_bytes = tp::kDefaultMaxHttpBodyBytes,
                                                },
                                                make_default_tls_credential_provider());
  bad_address_server.start();
  EXPECT_FALSE(bad_address_server.running());

  BeastHttpWorkflowApiServer first_server(runtime_service,
                                          tp::HttpEndpointOptions{
                                              .bind_address = tp::kDefaultLoopbackAddress,
                                              .port = 0,
                                              .use_tls = false,
                                              .tls = {},
                                              .io_threads = tp::kDefaultIoThreads,
                                              .max_body_bytes = tp::kDefaultMaxHttpBodyBytes,
                                          },
                                          make_default_tls_credential_provider());
  first_server.start();
  ASSERT_TRUE(first_server.running());

  BeastHttpWorkflowApiServer conflicting_server(runtime_service,
                                                tp::HttpEndpointOptions{
                                                    .bind_address = tp::kDefaultLoopbackAddress,
                                                    .port = port_from_endpoint(first_server.endpoint()),
                                                    .use_tls = false,
                                                    .tls = {},
                                                    .io_threads = tp::kDefaultIoThreads,
                                                    .max_body_bytes = tp::kDefaultMaxHttpBodyBytes,
                                                },
                                                make_default_tls_credential_provider());
  conflicting_server.start();
  EXPECT_FALSE(conflicting_server.running());

  conflicting_server.stop();
  first_server.stop();
}

}  // namespace
}  // namespace task_orchestrator::protocol
