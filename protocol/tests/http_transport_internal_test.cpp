#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "src/detail/http_transport_detail.hpp"
#include "test_support_tls_material.hpp"

namespace task_orchestrator::protocol {
namespace {
namespace tp = task_orchestrator::protocol;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

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

GetOperatorDashboardResponse make_dashboard_response(std::string workflow_id) {
  GetOperatorDashboardResponse response;
  response.set_ok(true);
  response.set_selected_workflow_id(std::move(workflow_id));
  response.mutable_stats()->set_workflows_tracked(1);
  return response;
}

OperatorDashboardUpdate make_dashboard_update(std::string workflow_id) {
  OperatorDashboardUpdate update;
  update.set_ok(true);
  update.set_server_time_unix_ms(1711708920000);
  update.set_selected_workflow_id(std::move(workflow_id));
  update.mutable_stats()->set_workflows_tracked(1);
  update.add_connectors()->set_id("webhook");
  return update;
}

OperatorMutationResponse make_operator_mutation_response(std::string workflow_id) {
  OperatorMutationResponse response;
  response.set_ok(true);
  response.mutable_dashboard()->set_ok(true);
  response.mutable_dashboard()->set_selected_workflow_id(std::move(workflow_id));
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

class CapturingWorkflowOperatorService final : public WorkflowOperatorService, public WorkflowOperatorEventService {
 public:
  GetOperatorDashboardResponse get_dashboard(const GetOperatorDashboardRequest& request) override {
    last_dashboard = request;
    ++dashboard_calls;
    return dashboard_response;
  }

  OperatorDashboardUpdate get_dashboard_update(const GetOperatorDashboardRequest& request,
                                               const OperatorDashboardNotification& notification) override {
    last_dashboard_update_request = request;
    last_dashboard_notification = notification;
    ++dashboard_update_calls;
    dashboard_update_response.set_selected_workflow_id(notification.workflow_id);
    return dashboard_update_response;
  }

  OperatorMutationResponse upsert_workflow(const UpsertOperatorWorkflowRequest& request) override {
    last_workflow = request;
    ++upsert_workflow_calls;
    return workflow_response;
  }

  OperatorMutationResponse upsert_task(const UpsertOperatorTaskRequest& request) override {
    last_task = request;
    ++upsert_task_calls;
    return task_response;
  }

  OperatorMutationResponse delete_task(const DeleteOperatorTaskRequest& request) override {
    last_delete_task = request;
    ++delete_task_calls;
    return delete_task_response;
  }

  OperatorMutationResponse pause_workflow(const OperatorWorkflowActionRequest& request) override {
    last_pause = request;
    ++pause_calls;
    return pause_response;
  }

  OperatorMutationResponse resume_workflow(const OperatorWorkflowActionRequest& request) override {
    last_resume = request;
    ++resume_calls;
    return resume_response;
  }

  OperatorMutationResponse cancel_workflow(const OperatorWorkflowActionRequest& request) override {
    last_cancel = request;
    ++cancel_calls;
    return cancel_response;
  }

  OperatorMutationResponse apply_manual_intervention(const ManualInterventionRequest& request) override {
    last_intervention = request;
    ++intervention_calls;
    return intervention_response;
  }

  std::uint64_t latest_dashboard_event_id() const override {
    std::scoped_lock lock(mutex_);
    return latest_dashboard_event_id_;
  }

  std::optional<OperatorDashboardNotification> wait_for_dashboard_update(
      const std::uint64_t after_event_id, const std::chrono::milliseconds timeout) override {
    std::unique_lock lock(mutex_);
    const auto ready = [this, after_event_id]() { return latest_dashboard_event_id_ > after_event_id; };
    if (!ready() && !condition_variable_.wait_for(lock, timeout, ready)) {
      return std::nullopt;
    }
    return latest_notification_;
  }

  void publish_dashboard_update(std::string workflow_id) {
    std::scoped_lock lock(mutex_);
    latest_notification_ = OperatorDashboardNotification{
        .event_id = ++latest_dashboard_event_id_,
        .workflow_id = std::move(workflow_id),
    };
    condition_variable_.notify_all();
  }

  GetOperatorDashboardRequest last_dashboard;
  GetOperatorDashboardRequest last_dashboard_update_request;
  OperatorDashboardNotification last_dashboard_notification;
  UpsertOperatorWorkflowRequest last_workflow;
  UpsertOperatorTaskRequest last_task;
  DeleteOperatorTaskRequest last_delete_task;
  OperatorWorkflowActionRequest last_pause;
  OperatorWorkflowActionRequest last_resume;
  OperatorWorkflowActionRequest last_cancel;
  ManualInterventionRequest last_intervention;
  GetOperatorDashboardResponse dashboard_response = make_dashboard_response("wf-live");
  OperatorDashboardUpdate dashboard_update_response = make_dashboard_update("wf-live");
  OperatorMutationResponse workflow_response = make_operator_mutation_response("wf-live");
  OperatorMutationResponse task_response = make_operator_mutation_response("wf-live");
  OperatorMutationResponse delete_task_response = make_operator_mutation_response("wf-live");
  OperatorMutationResponse pause_response = make_operator_mutation_response("wf-live");
  OperatorMutationResponse resume_response = make_operator_mutation_response("wf-live");
  OperatorMutationResponse cancel_response = make_operator_mutation_response("wf-live");
  OperatorMutationResponse intervention_response = make_operator_mutation_response("wf-live");
  int dashboard_calls = 0;
  int dashboard_update_calls = 0;
  int upsert_workflow_calls = 0;
  int upsert_task_calls = 0;
  int delete_task_calls = 0;
  int pause_calls = 0;
  int resume_calls = 0;
  int cancel_calls = 0;
  int intervention_calls = 0;

 private:
  mutable std::mutex mutex_;
  std::condition_variable condition_variable_;
  std::uint64_t latest_dashboard_event_id_ = 0;
  std::optional<OperatorDashboardNotification> latest_notification_;
};

struct FailingSerializableMessage {
  static bool SerializeToString(std::string*) { return false; }  // NOLINT(readability-identifier-naming)
};

int port_from_endpoint(const std::string& endpoint) {
  return std::stoi(endpoint.substr(endpoint.find_last_of(':') + 1));
}

std::string read_socket_until(tcp::socket& socket,
                              const std::string_view needle,
                              const std::chrono::milliseconds timeout) {
  socket.non_blocking(true);
  std::string data;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    beast::error_code error_code;
    const auto available = socket.available(error_code);
    if (error_code) {
      break;
    }
    if (available == 0U) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    std::string chunk(available, '\0');
    const auto bytes_read = socket.read_some(asio::buffer(chunk), error_code);
    if (error_code && error_code != asio::error::would_block && error_code != asio::error::try_again) {
      break;
    }
    chunk.resize(bytes_read);
    data += chunk;
    if (data.find(needle) != std::string::npos) {
      break;
    }
  }
  return data;
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

TEST(HttpTransportInternalTest, QueryDecodingAndOperatorRouteHelpersRejectInvalidShapes) {
  EXPECT_EQ("lane blocked/wave", detail::decode_query_component("lane+blocked%2Fwave"));
  EXPECT_EQ("A", detail::decode_query_component("%41"));
  EXPECT_EQ("%ZZ", detail::decode_query_component("%ZZ"));

  EXPECT_EQ("wf-1", detail::query_value("selected_workflow_id=wf-1", "selected_workflow_id").value());
  EXPECT_EQ("lane blocked",
            detail::query_value("workflowQuery=lane+blocked", "workflow_query", "workflowQuery").value());
  EXPECT_FALSE(detail::query_value("workflow_query=lane", "selected_workflow_id").has_value());

  EXPECT_EQ(7, detail::query_int32("workflow_page_size=7", "workflow_page_size"));
  EXPECT_EQ(0, detail::query_int32("workflowPageSize=oops", "workflow_page_size", "workflowPageSize"));
  EXPECT_EQ(0, detail::query_int32("", "workflow_page_size"));

  EXPECT_EQ("wf-live", detail::extract_operator_workflow_id("/v1/operator/workflows/wf-live:pause", ":pause").value());
  EXPECT_FALSE(detail::extract_operator_workflow_id("/v1/operator/workflows/:pause", ":pause").has_value());
  EXPECT_FALSE(
      detail::extract_operator_workflow_id("/v1/operator/workflows/wf-live/tasks:pause", ":pause").has_value());

  EXPECT_EQ("wf-live", detail::extract_operator_task_workflow_id("/v1/operator/workflows/wf-live/tasks").value());
  EXPECT_FALSE(detail::extract_operator_task_workflow_id("/v1/operator/workflows//tasks").has_value());
  EXPECT_FALSE(detail::extract_operator_task_workflow_id("/v1/operator/workflows/wf-live/tasks/extra").has_value());

  const auto delete_route =
      detail::extract_operator_task_delete_route("/v1/operator/workflows/wf-live/tasks/pick-1:delete");
  ASSERT_TRUE(delete_route.has_value());
  EXPECT_EQ("wf-live", delete_route->workflow_id);
  EXPECT_EQ("pick-1", delete_route->task_id);
  EXPECT_FALSE(detail::extract_operator_task_delete_route("/v1/operator/workflows/wf-live/tasks/:delete").has_value());
  EXPECT_FALSE(detail::extract_operator_task_delete_route("/v1/operator/workflows/wf-live:delete").has_value());

  http::request<http::string_body> sse_request(http::verb::get, "/v1/operator/dashboard:stream", 11);
  EXPECT_TRUE(detail::is_operator_dashboard_stream_request(sse_request));
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

  const auto submit_response = detail::handle_http_request(runtime_service, nullptr, false, http_submit_request);
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
            detail::handle_http_request(runtime_service, nullptr, true, mismatch_http_request).result());

  tp::ReorchestrateRequest reorchestrate_request;
  ASSERT_TRUE(detail::serialize_message(reorchestrate_request, &replan_body, &content_type, &error_message));

  http::request<http::string_body> good_http_request(http::verb::post, "/v1/workflows/path_id:reorchestrate", 11);
  good_http_request.set(tp::kApiKeyHeader, "secure-api-key");
  good_http_request.body() = replan_body;
  good_http_request.prepare_payload();

  const auto reorchestrate_response = detail::handle_http_request(runtime_service, nullptr, true, good_http_request);
  EXPECT_EQ(http::status::ok, reorchestrate_response.result());
  EXPECT_EQ(1, runtime_service.reorchestrate_calls);
  EXPECT_EQ("path_id", runtime_service.last_reorchestrate.workflow_id());
  EXPECT_TRUE(runtime_service.last_reorchestrate.auth().secure_transport());
  EXPECT_EQ("secure-api-key", runtime_service.last_reorchestrate.auth().api_key());

  http::request<http::string_body> wrong_method(http::verb::get, std::string(tp::kHttpSubmitWorkflowPath), 11);
  EXPECT_EQ(http::status::method_not_allowed,
            detail::handle_http_request(runtime_service, nullptr, false, wrong_method).result());

  http::request<http::string_body> unknown_route(http::verb::post, "/v1/unsupported", 11);
  unknown_route.prepare_payload();
  EXPECT_EQ(http::status::not_found,
            detail::handle_http_request(runtime_service, nullptr, false, unknown_route).result());
}

TEST(HttpTransportInternalTest, OperatorRoutesAcceptJsonPayloadsAndQueryParameters) {
  CapturingWorkflowRuntimeService runtime_service;
  CapturingWorkflowOperatorService operator_service;

  http::request<http::string_body> dashboard_request(
      http::verb::get,
      "/v1/operator/dashboard?selectedWorkflowId=wf-live&workflowQuery=lane+blocked&workflowPageSize=7&maxEvents=5&"
      "maxPlanVersions=3&maxAuditEntries=2",
      11);
  dashboard_request.set(tp::kAuthorizationHeader, "Bearer operator-token");
  dashboard_request.prepare_payload();

  const auto dashboard_response =
      detail::handle_http_request(runtime_service, &operator_service, true, dashboard_request);
  EXPECT_EQ(http::status::ok, dashboard_response.result());
  EXPECT_EQ(1, operator_service.dashboard_calls);
  EXPECT_EQ("wf-live", operator_service.last_dashboard.selected_workflow_id());
  EXPECT_EQ("lane blocked", operator_service.last_dashboard.workflow_query());
  EXPECT_EQ(7, operator_service.last_dashboard.workflow_page_size());
  EXPECT_EQ(5, operator_service.last_dashboard.max_events());
  EXPECT_EQ(3, operator_service.last_dashboard.max_plan_versions());
  EXPECT_EQ(2, operator_service.last_dashboard.max_audit_entries());
  EXPECT_EQ("operator-token", operator_service.last_dashboard.auth().bearer_token());
  EXPECT_TRUE(operator_service.last_dashboard.auth().secure_transport());

  GetOperatorDashboardResponse parsed_dashboard;
  std::string parse_error;
  ASSERT_TRUE(detail::parse_json_message(dashboard_response.body(), &parsed_dashboard, &parse_error));
  EXPECT_TRUE(parsed_dashboard.ok());
  EXPECT_EQ("wf-live", parsed_dashboard.selected_workflow_id());

  http::request<http::string_body> workflow_request(http::verb::post, std::string(tp::kHttpOperatorWorkflowsPath), 11);
  workflow_request.body() =
      R"({"config":{"id":"wf-live","actors":[{"id":"robot_1","type":"robot"}],"tasks":[{"id":"pick-1","requestedTime":"10","duration":"20"}]},"note":"clone workflow"})";
  workflow_request.prepare_payload();
  const auto workflow_response =
      detail::handle_http_request(runtime_service, &operator_service, false, workflow_request);
  EXPECT_EQ(http::status::ok, workflow_response.result());
  EXPECT_EQ(1, operator_service.upsert_workflow_calls);
  EXPECT_EQ("wf-live", operator_service.last_workflow.config().id());
  EXPECT_EQ(1, operator_service.last_workflow.config().actors_size());
  EXPECT_EQ(1, operator_service.last_workflow.config().tasks_size());
  EXPECT_EQ("clone workflow", operator_service.last_workflow.note());

  http::request<http::string_body> task_request(http::verb::post, "/v1/operator/workflows/wf-live/tasks", 11);
  task_request.body() =
      R"({"workflowId":"wf-live","task":{"id":"pick-9","requestedTime":"1000","duration":"600000"},"note":"insert task"})";
  task_request.prepare_payload();
  const auto task_response = detail::handle_http_request(runtime_service, &operator_service, false, task_request);
  EXPECT_EQ(http::status::ok, task_response.result());
  EXPECT_EQ(1, operator_service.upsert_task_calls);
  EXPECT_EQ("wf-live", operator_service.last_task.workflow_id());
  EXPECT_EQ("pick-9", operator_service.last_task.task().id());
  EXPECT_EQ("insert task", operator_service.last_task.note());

  http::request<http::string_body> intervention_request(
      http::verb::post, "/v1/operator/workflows/wf-live:manualIntervention", 11);
  intervention_request.body() = R"({"workflowId":"wf-live","note":"freeze lane","triggerReorchestration":true})";
  intervention_request.prepare_payload();
  const auto intervention_response =
      detail::handle_http_request(runtime_service, &operator_service, false, intervention_request);
  EXPECT_EQ(http::status::ok, intervention_response.result());
  EXPECT_EQ(1, operator_service.intervention_calls);
  EXPECT_EQ("wf-live", operator_service.last_intervention.workflow_id());
  EXPECT_EQ("freeze lane", operator_service.last_intervention.note());
  EXPECT_TRUE(operator_service.last_intervention.trigger_reorchestration());
}

TEST(HttpTransportInternalTest, OperatorRoutesRejectUnavailableMalformedAndMismatchedRequests) {
  CapturingWorkflowRuntimeService runtime_service;
  CapturingWorkflowOperatorService operator_service;

  http::request<http::string_body> unavailable_dashboard_request(
      http::verb::get, std::string(tp::kHttpOperatorDashboardPath), 11);
  unavailable_dashboard_request.prepare_payload();
  const auto unavailable_dashboard_response =
      detail::handle_http_request(runtime_service, nullptr, false, unavailable_dashboard_request);
  EXPECT_EQ(http::status::service_unavailable, unavailable_dashboard_response.result());

  GetOperatorDashboardResponse parsed_dashboard_error;
  std::string parse_error;
  ASSERT_TRUE(detail::parse_json_message(unavailable_dashboard_response.body(), &parsed_dashboard_error, &parse_error));
  EXPECT_FALSE(parsed_dashboard_error.ok());
  EXPECT_NE(parsed_dashboard_error.error_message().find("control plane is not enabled"), std::string::npos);

  http::request<http::string_body> invalid_workflow_request(
      http::verb::post, std::string(tp::kHttpOperatorWorkflowsPath), 11);
  invalid_workflow_request.body() = R"({"config":)";
  invalid_workflow_request.prepare_payload();
  const auto invalid_workflow_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_workflow_request);
  EXPECT_EQ(http::status::bad_request, invalid_workflow_response.result());

  OperatorMutationResponse parsed_mutation_error;
  ASSERT_TRUE(detail::parse_json_message(invalid_workflow_response.body(), &parsed_mutation_error, &parse_error));
  EXPECT_FALSE(parsed_mutation_error.ok());
  EXPECT_NE(parsed_mutation_error.error_message().find("Failed to parse JSON request body"), std::string::npos);

  http::request<http::string_body> mismatched_task_request(
      http::verb::post, "/v1/operator/workflows/wf-live/tasks", 11);
  mismatched_task_request.body() =
      R"({"workflowId":"wf-other","task":{"id":"pick-9","requestedTime":"1000","duration":"600000"}})";
  mismatched_task_request.prepare_payload();
  const auto mismatched_task_response =
      detail::handle_http_request(runtime_service, &operator_service, false, mismatched_task_request);
  EXPECT_EQ(http::status::bad_request, mismatched_task_response.result());
  EXPECT_EQ(0, operator_service.upsert_task_calls);

  ASSERT_TRUE(detail::parse_json_message(mismatched_task_response.body(), &parsed_mutation_error, &parse_error));
  EXPECT_FALSE(parsed_mutation_error.ok());
  EXPECT_NE(parsed_mutation_error.error_message().find("workflow_id in path does not match"), std::string::npos);

  http::request<http::string_body> invalid_delete_request(
      http::verb::post, "/v1/operator/workflows/wf-live/tasks/pick-9:delete", 11);
  invalid_delete_request.body() = R"({"workflowId":)";
  invalid_delete_request.prepare_payload();
  const auto invalid_delete_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_delete_request);
  EXPECT_EQ(http::status::bad_request, invalid_delete_response.result());

  http::request<http::string_body> mismatched_delete_request(
      http::verb::post, "/v1/operator/workflows/wf-live/tasks/pick-9:delete", 11);
  mismatched_delete_request.body() = R"({"workflowId":"wf-other","taskId":"pick-7"})";
  mismatched_delete_request.prepare_payload();
  const auto mismatched_delete_response =
      detail::handle_http_request(runtime_service, &operator_service, false, mismatched_delete_request);
  EXPECT_EQ(http::status::bad_request, mismatched_delete_response.result());
  EXPECT_EQ(0, operator_service.delete_task_calls);

  http::request<http::string_body> invalid_pause_request(http::verb::post, "/v1/operator/workflows/wf-live:pause", 11);
  invalid_pause_request.body() = R"({"workflowId":"wf-other"})";
  invalid_pause_request.prepare_payload();
  const auto invalid_pause_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_pause_request);
  EXPECT_EQ(http::status::bad_request, invalid_pause_response.result());
  EXPECT_EQ(0, operator_service.pause_calls);

  http::request<http::string_body> invalid_resume_request(
      http::verb::post, "/v1/operator/workflows/wf-live:resume", 11);
  invalid_resume_request.body() = R"({"workflowId":)";
  invalid_resume_request.prepare_payload();
  const auto invalid_resume_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_resume_request);
  EXPECT_EQ(http::status::bad_request, invalid_resume_response.result());
  EXPECT_EQ(0, operator_service.resume_calls);

  http::request<http::string_body> invalid_cancel_request(
      http::verb::post, "/v1/operator/workflows/wf-live:cancel", 11);
  invalid_cancel_request.body() = R"({"workflowId":"wf-other"})";
  invalid_cancel_request.prepare_payload();
  const auto invalid_cancel_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_cancel_request);
  EXPECT_EQ(http::status::bad_request, invalid_cancel_response.result());
  EXPECT_EQ(0, operator_service.cancel_calls);

  http::request<http::string_body> invalid_intervention_request(
      http::verb::post, "/v1/operator/workflows/wf-live:manualIntervention", 11);
  invalid_intervention_request.body() = R"({"workflowId":"wf-other","note":"freeze lane"})";
  invalid_intervention_request.prepare_payload();
  const auto invalid_intervention_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_intervention_request);
  EXPECT_EQ(http::status::bad_request, invalid_intervention_response.result());
  EXPECT_EQ(0, operator_service.intervention_calls);

  http::request<http::string_body> unknown_operator_route(
      http::verb::post, "/v1/operator/workflows/wf-live:unknown", 11);
  unknown_operator_route.prepare_payload();
  const auto unknown_operator_response =
      detail::handle_http_request(runtime_service, &operator_service, false, unknown_operator_route);
  EXPECT_EQ(http::status::not_found, unknown_operator_response.result());

  ASSERT_TRUE(detail::parse_json_message(unknown_operator_response.body(), &parsed_dashboard_error, &parse_error));
  EXPECT_FALSE(parsed_dashboard_error.ok());
  EXPECT_NE(parsed_dashboard_error.error_message().find("Unsupported operator route"), std::string::npos);
}

TEST(HttpTransportInternalTest, OperatorRoutesSupportDeleteAndLifecycleMutations) {
  CapturingWorkflowRuntimeService runtime_service;
  CapturingWorkflowOperatorService operator_service;

  http::request<http::string_body> delete_request(
      http::verb::post, "/v1/operator/workflows/wf-live/tasks/pick-9:delete", 11);
  delete_request.set(tp::kAuthorizationHeader, "Bearer operator-token");
  delete_request.prepare_payload();
  const auto delete_response = detail::handle_http_request(runtime_service, &operator_service, true, delete_request);
  EXPECT_EQ(http::status::ok, delete_response.result());
  EXPECT_EQ(1, operator_service.delete_task_calls);
  EXPECT_EQ("wf-live", operator_service.last_delete_task.workflow_id());
  EXPECT_EQ("pick-9", operator_service.last_delete_task.task_id());
  EXPECT_EQ("operator-token", operator_service.last_delete_task.auth().bearer_token());
  EXPECT_TRUE(operator_service.last_delete_task.auth().secure_transport());

  http::request<http::string_body> pause_request(http::verb::post, "/v1/operator/workflows/wf-live:pause", 11);
  pause_request.body() = R"({"reason":"pause for inspection"})";
  pause_request.prepare_payload();
  const auto pause_response = detail::handle_http_request(runtime_service, &operator_service, false, pause_request);
  EXPECT_EQ(http::status::ok, pause_response.result());
  EXPECT_EQ(1, operator_service.pause_calls);
  EXPECT_EQ("wf-live", operator_service.last_pause.workflow_id());
  EXPECT_EQ("pause for inspection", operator_service.last_pause.reason());

  http::request<http::string_body> resume_request(http::verb::post, "/v1/operator/workflows/wf-live:resume", 11);
  resume_request.body() = "{}";
  resume_request.prepare_payload();
  const auto resume_response = detail::handle_http_request(runtime_service, &operator_service, false, resume_request);
  EXPECT_EQ(http::status::ok, resume_response.result());
  EXPECT_EQ(1, operator_service.resume_calls);
  EXPECT_EQ("wf-live", operator_service.last_resume.workflow_id());

  http::request<http::string_body> cancel_request(http::verb::post, "/v1/operator/workflows/wf-live:cancel", 11);
  cancel_request.body() = R"({"reason":"cancel wave"})";
  cancel_request.prepare_payload();
  const auto cancel_response = detail::handle_http_request(runtime_service, &operator_service, false, cancel_request);
  EXPECT_EQ(http::status::ok, cancel_response.result());
  EXPECT_EQ(1, operator_service.cancel_calls);
  EXPECT_EQ("wf-live", operator_service.last_cancel.workflow_id());
  EXPECT_EQ("cancel wave", operator_service.last_cancel.reason());

  http::request<http::string_body> mismatched_resume_request(
      http::verb::post, "/v1/operator/workflows/wf-live:resume", 11);
  mismatched_resume_request.body() = R"({"workflowId":"wf-other"})";
  mismatched_resume_request.prepare_payload();
  const auto mismatched_resume_response =
      detail::handle_http_request(runtime_service, &operator_service, false, mismatched_resume_request);
  EXPECT_EQ(http::status::bad_request, mismatched_resume_response.result());

  http::request<http::string_body> invalid_cancel_request(
      http::verb::post, "/v1/operator/workflows/wf-live:cancel", 11);
  invalid_cancel_request.body() = R"({"workflowId":)";
  invalid_cancel_request.prepare_payload();
  const auto invalid_cancel_response =
      detail::handle_http_request(runtime_service, &operator_service, false, invalid_cancel_request);
  EXPECT_EQ(http::status::bad_request, invalid_cancel_response.result());

  OperatorMutationResponse parsed_mutation_response;
  std::string parse_error;
  ASSERT_TRUE(detail::parse_json_message(cancel_response.body(), &parsed_mutation_response, &parse_error));
  EXPECT_TRUE(parsed_mutation_response.ok());
  EXPECT_EQ("wf-live", parsed_mutation_response.dashboard().selected_workflow_id());
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

TEST(HttpTransportInternalTest, HttpServerStreamsOperatorDashboardSnapshotsOverSse) {
  CapturingWorkflowRuntimeService runtime_service;
  CapturingWorkflowOperatorService operator_service;

  BeastHttpWorkflowApiServer server(runtime_service,
                                    tp::HttpEndpointOptions{
                                        .bind_address = tp::kDefaultLoopbackAddress,
                                        .port = 0,
                                        .use_tls = false,
                                        .tls = {},
                                        .io_threads = tp::kDefaultIoThreads,
                                        .max_body_bytes = tp::kDefaultMaxHttpBodyBytes,
                                    },
                                    make_default_tls_credential_provider(),
                                    &operator_service,
                                    &operator_service);
  server.start();
  ASSERT_TRUE(server.running());

  asio::io_context io_context;
  tcp::resolver resolver(io_context);
  tcp::socket socket(io_context);
  beast::error_code error_code;
  const auto resolved =
      resolver.resolve(tp::kDefaultLoopbackAddress, std::to_string(port_from_endpoint(server.endpoint())), error_code);
  ASSERT_FALSE(error_code);
  asio::connect(socket, resolved, error_code);
  ASSERT_FALSE(error_code);

  http::request<http::string_body> request(
      http::verb::get, "/v1/operator/dashboard:stream?selectedWorkflowId=wf-live", 11);
  request.set(http::field::host, tp::kDefaultLoopbackAddress);
  request.set(http::field::accept, "text/event-stream");
  request.prepare_payload();
  http::write(socket, request, error_code);
  ASSERT_FALSE(error_code);

  const std::string initial = read_socket_until(socket, R"("selectedWorkflowId":"wf-live")", std::chrono::seconds(1));
  EXPECT_NE(initial.find("text/event-stream"), std::string::npos);
  EXPECT_NE(initial.find("id: 0"), std::string::npos);
  EXPECT_NE(initial.find("event: dashboard"), std::string::npos);
  EXPECT_NE(initial.find("\"selectedWorkflowId\":\"wf-live\""), std::string::npos);
  EXPECT_EQ(1, operator_service.dashboard_calls);
  EXPECT_EQ(0, operator_service.dashboard_update_calls);

  operator_service.publish_dashboard_update("wf-live");
  const std::string updated = read_socket_until(socket, "id: 1", std::chrono::seconds(1));
  EXPECT_NE(updated.find("id: 1"), std::string::npos);
  EXPECT_NE(updated.find("event: dashboard-update"), std::string::npos);
  EXPECT_NE(updated.find("\"selectedWorkflowId\":\"wf-live\""), std::string::npos);
  EXPECT_EQ(1, operator_service.dashboard_calls);
  EXPECT_EQ(1, operator_service.dashboard_update_calls);
  EXPECT_EQ("wf-live", operator_service.last_dashboard_update_request.selected_workflow_id());
  EXPECT_EQ("wf-live", operator_service.last_dashboard_notification.workflow_id);

  socket.close();
  server.stop();
}

}  // namespace
}  // namespace task_orchestrator::protocol
