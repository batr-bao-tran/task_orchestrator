#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "protocol/test_support_tls_material.hpp"
#include "src/detail/grpc_transport_detail.hpp"

namespace task_orchestrator::protocol {
namespace {
namespace tp = task_orchestrator::protocol;

const tp::test_support::TestTlsMaterial& test_tls_material() noexcept {
  return tp::test_support::localhost_tls_material();
}

template <typename Result>
std::future<Result> make_test_ready_future(Result value) {
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

class StubWorkflowRuntimeService final : public WorkflowRuntimeService {
 public:
  RuntimeApiResponse submit_workflow(const SubmitWorkflowRequest&) override { return submit_response; }

  RuntimeApiResponse reorchestrate(const ReorchestrateRequest&) override { return reorchestrate_response; }

  std::future<RuntimeApiResponse> submit_workflow_async(const SubmitWorkflowRequest&) override {
    return make_test_ready_future(submit_response);
  }

  std::future<RuntimeApiResponse> reorchestrate_async(const ReorchestrateRequest&) override {
    return make_test_ready_future(reorchestrate_response);
  }

  WorkflowEventStream stream_submit_workflow(SubmitWorkflowRequest) override { co_return; }

  WorkflowEventStream stream_reorchestrate(ReorchestrateRequest) override { co_return; }

  RuntimeApiResponse submit_response = make_ok_response("submit ok");
  RuntimeApiResponse reorchestrate_response = make_ok_response("replan ok");
};

class InvalidMutualTlsProvider final : public TlsCredentialProvider {
 public:
  TlsServerLoadResult load_server_credentials(const TlsServerConfig&) const override {
    return TlsServerLoadResult{
        .value =
            {
                .identity =
                    {
                        .certificate_chain_pem = test_tls_material().certificate_chain_pem,
                        .private_key_pem = test_tls_material().private_key_pem,
                        .private_key_password = {},
                    },
                .client_trust =
                    {
                        .root_certificates_pem = {},
                        .use_system_default_roots = false,
                        .verify_peer = false,
                        .expected_peer_name = {},
                    },
                .require_client_certificate = true,
            },
        .error_message = {},
    };
  }

  TlsClientLoadResult load_client_credentials(const TlsClientConfig&) const override {
    return TlsClientLoadResult{
        .value = {},
        .error_message = "client tls unavailable",
    };
  }
};

class FailingClientTlsProvider final : public TlsCredentialProvider {
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

TEST(GrpcTransportInternalTest, ErrorResponseAndTargetHelpersAreDeterministic) {
  const auto response = detail::make_transport_error_response("grpc failed");
  EXPECT_FALSE(response.ok());
  EXPECT_EQ("grpc failed", response.error_message());
  EXPECT_EQ("grpc failed", response.result().error_message());

  EXPECT_EQ("host.example:1234", detail::make_grpc_target("host.example", 1234));

  const tp::GrpcClientOptions options{
      .host = "service.internal",
      .port = 443,
      .use_tls = true,
      .tls = {},
      .deadline_ms = tp::kDefaultRequestTimeoutMs,
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
}

TEST(GrpcTransportInternalTest, CredentialFactoriesValidateSuccessAndFailureCases) {
  ASSERT_TRUE(test_tls_material().ok) << test_tls_material().error_message;
  const tp::LoadedTlsServerConfig server_tls{
      .identity =
          {
              .certificate_chain_pem = test_tls_material().certificate_chain_pem,
              .private_key_pem = test_tls_material().private_key_pem,
              .private_key_password = {},
          },
      .client_trust =
          {
              .root_certificates_pem = test_tls_material().certificate_chain_pem,
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = {},
          },
      .require_client_certificate = true,
  };
  const auto valid_server_credentials = detail::make_server_credentials(server_tls);
  EXPECT_TRUE(valid_server_credentials.ok());
  EXPECT_NE(nullptr, valid_server_credentials.server_credentials);

  const auto invalid_server_credentials = detail::make_server_credentials(tp::LoadedTlsServerConfig{
      .identity = server_tls.identity,
      .client_trust =
          {
              .root_certificates_pem = {},
              .use_system_default_roots = false,
              .verify_peer = false,
              .expected_peer_name = {},
          },
      .require_client_certificate = true,
  });
  EXPECT_FALSE(invalid_server_credentials.ok());
  EXPECT_NE(invalid_server_credentials.error_message.find("mutual TLS requires client certificate verification"),
            std::string::npos);

  const tp::LoadedTlsClientConfig client_tls{
      .identity =
          {
              .certificate_chain_pem = test_tls_material().certificate_chain_pem,
              .private_key_pem = test_tls_material().private_key_pem,
              .private_key_password = {},
          },
      .server_trust =
          {
              .root_certificates_pem = test_tls_material().certificate_chain_pem,
              .use_system_default_roots = false,
              .verify_peer = true,
              .expected_peer_name = "localhost",
          },
  };
  const auto valid_client_credentials = detail::make_client_credentials(client_tls);
  EXPECT_TRUE(valid_client_credentials.ok());
  EXPECT_NE(nullptr, valid_client_credentials.channel_credentials);

  const auto invalid_client_credentials = detail::make_client_credentials(tp::LoadedTlsClientConfig{
      .identity = {},
      .server_trust =
          {
              .root_certificates_pem = test_tls_material().certificate_chain_pem,
              .use_system_default_roots = false,
              .verify_peer = false,
              .expected_peer_name = "localhost",
          },
  });
  EXPECT_FALSE(invalid_client_credentials.ok());
  EXPECT_NE(invalid_client_credentials.error_message.find("requires peer verification"), std::string::npos);
}

TEST(GrpcTransportInternalTest, ServerAndClientInitializationFailuresReturnStructuredErrors) {
  StubWorkflowRuntimeService runtime_service;

  auto invalid_server_provider = std::make_shared<InvalidMutualTlsProvider>();
  GrpcWorkflowApiServer invalid_server(runtime_service,
                                       tp::GrpcEndpointOptions{
                                           .bind_address = tp::kDefaultLoopbackAddress,
                                           .port = 0,
                                           .use_tls = true,
                                           .tls = {},
                                           .max_receive_message_bytes = static_cast<int>(tp::kDefaultMaxHttpBodyBytes),
                                           .max_send_message_bytes = static_cast<int>(tp::kDefaultMaxHttpBodyBytes),
                                       },
                                       invalid_server_provider);
  invalid_server.start();
  EXPECT_FALSE(invalid_server.running());

  auto failing_client_provider = std::make_shared<FailingClientTlsProvider>();
  GrpcWorkflowApiClient failing_client(
      tp::GrpcClientOptions{
          .host = tp::kDefaultLoopbackAddress,
          .port = tp::kDefaultGrpcPort,
          .use_tls = true,
          .tls = {},
          .deadline_ms = tp::kDefaultRequestTimeoutMs,
          .bearer_token = {},
          .api_key = {},
      },
      failing_client_provider,
      nullptr);
  const auto startup_failure = failing_client.submit_async(tp::SubmitWorkflowRequest{}).get();
  EXPECT_FALSE(startup_failure.ok());
  EXPECT_NE(startup_failure.error_message().find("gRPC transport failed"), std::string::npos);

  std::vector<WorkflowEvent> startup_failure_events;
  for (WorkflowEvent event : failing_client.submit_stream(tp::SubmitWorkflowRequest{})) {
    startup_failure_events.push_back(std::move(event));
  }
  ASSERT_EQ(1U, startup_failure_events.size());
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, startup_failure_events.front().type());
  ASSERT_TRUE(startup_failure_events.front().has_response());
  EXPECT_FALSE(startup_failure_events.front().response().ok());
  EXPECT_NE(startup_failure_events.front().response().error_message().find("gRPC transport failed"), std::string::npos);

  const auto missing_stub_response = detail::validate_grpc_client_invocation_state("", nullptr);
  ASSERT_TRUE(missing_stub_response.has_value());
  EXPECT_FALSE(missing_stub_response->ok());
  EXPECT_NE(missing_stub_response->error_message().find("client stub is not initialized"), std::string::npos);

  tp::ReorchestrateRequest reorchestrate_request;
  reorchestrate_request.set_workflow_id("workflow_demo");
  std::vector<WorkflowEvent> reorchestrate_failure_events;
  for (WorkflowEvent event : failing_client.reorchestrate_stream(reorchestrate_request)) {
    reorchestrate_failure_events.push_back(std::move(event));
  }
  ASSERT_EQ(1U, reorchestrate_failure_events.size());
  EXPECT_EQ("workflow_demo", reorchestrate_failure_events.front().workflow_id());
  ASSERT_TRUE(reorchestrate_failure_events.front().has_response());
  EXPECT_FALSE(reorchestrate_failure_events.front().response().ok());
}

}  // namespace
}  // namespace task_orchestrator::protocol
