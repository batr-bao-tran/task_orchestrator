#ifndef TASK_ORCHESTRATOR__PROTOCOL_SRC_DETAIL__GRPC_TRANSPORT_DETAIL_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_SRC_DETAIL__GRPC_TRANSPORT_DETAIL_HPP_

#include <grpcpp/grpcpp.h>

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "protocol/grpc_transport.hpp"
#include "task_orchestration_service/task_orchestration_service.grpc.pb.h"

namespace task_orchestrator::protocol::detail {

inline constexpr std::string_view kBearerPrefix = "Bearer ";

template <typename Result>
std::future<Result> make_ready_future(Result value) {
  std::promise<Result> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

inline RuntimeApiResponse make_transport_error_response(std::string error_message) {
  RuntimeApiResponse response;
  response.set_ok(false);
  response.set_error_message(std::move(error_message));
  response.mutable_result()->set_ok(false);
  response.mutable_result()->set_error_message(response.error_message());
  return response;
}

struct GrpcCredentialsBuildResult {
  std::shared_ptr<grpc::ServerCredentials> server_credentials;
  std::shared_ptr<grpc::ChannelCredentials> channel_credentials;
  std::string error_message;

  [[nodiscard]] bool ok() const noexcept { return error_message.empty(); }
};

inline GrpcCredentialsBuildResult make_server_credentials(const LoadedTlsServerConfig& tls_config) {
  if (tls_config.require_client_certificate && !tls_config.client_trust.verify_peer) {
    return {
        .server_credentials = {},
        .channel_credentials = {},
        .error_message = "gRPC mutual TLS requires client certificate verification.",
    };
  }

  grpc::SslServerCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = tls_config.client_trust.root_certificates_pem;
  grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
  key_cert_pair.private_key = tls_config.identity.private_key_pem;
  key_cert_pair.cert_chain = tls_config.identity.certificate_chain_pem;
  ssl_options.pem_key_cert_pairs.push_back(std::move(key_cert_pair));
  ssl_options.client_certificate_request = tls_config.require_client_certificate
                                               ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
                                               : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  return {
      .server_credentials = grpc::SslServerCredentials(ssl_options),
      .channel_credentials = {},
      .error_message = {},
  };
}

inline GrpcCredentialsBuildResult make_client_credentials(const LoadedTlsClientConfig& tls_config) {
  if (!tls_config.server_trust.verify_peer) {
    return {
        .server_credentials = {},
        .channel_credentials = {},
        .error_message =
            "gRPC TLS requires peer verification; use insecure transport instead of disabling TLS verification.",
    };
  }

  grpc::SslCredentialsOptions ssl_options;
  ssl_options.pem_root_certs = tls_config.server_trust.root_certificates_pem;
  ssl_options.pem_private_key = tls_config.identity.private_key_pem;
  ssl_options.pem_cert_chain = tls_config.identity.certificate_chain_pem;
  return {
      .server_credentials = {},
      .channel_credentials = grpc::SslCredentials(ssl_options),
      .error_message = {},
  };
}

inline std::string resolve_expected_peer_name(const GrpcClientOptions& options,
                                              const LoadedTlsClientConfig& tls_config) {
  return tls_config.server_trust.expected_peer_name.empty() ? options.host : tls_config.server_trust.expected_peer_name;
}

template <typename Request>
void apply_metadata_auth(const grpc::ServerContext& context, bool secure_transport, Request* request) {
  pb::ClientAuthContext* auth = request->mutable_auth();
  const auto authorization_it = context.client_metadata().find(std::string(kAuthorizationHeader));
  if (authorization_it != context.client_metadata().end() && auth->bearer_token().empty()) {
    const std::string authorization_value(authorization_it->second.data(), authorization_it->second.length());
    if (authorization_value.starts_with(kBearerPrefix)) {
      auth->set_bearer_token(authorization_value.substr(kBearerPrefix.size()));
    }
  }
  const auto api_key_it = context.client_metadata().find(std::string(kApiKeyHeader));
  if (api_key_it != context.client_metadata().end() && auth->api_key().empty()) {
    auth->set_api_key(std::string(api_key_it->second.data(), api_key_it->second.length()));
  }
  auth->set_secure_transport(secure_transport);
}

inline std::string make_grpc_target(const std::string& host, int port) { return host + ":" + std::to_string(port); }

inline std::optional<RuntimeApiResponse> validate_grpc_client_invocation_state(
    std::string_view startup_error_message, const pb::WorkflowRuntimeApi::StubInterface* stub) {
  if (!startup_error_message.empty()) {
    return make_transport_error_response(std::string("gRPC transport failed: ") + std::string(startup_error_message));
  }
  if (stub == nullptr) {
    return make_transport_error_response("gRPC transport failed: client stub is not initialized.");
  }
  return std::nullopt;
}

}  // namespace task_orchestrator::protocol::detail

#endif  // TASK_ORCHESTRATOR__PROTOCOL_SRC_DETAIL__GRPC_TRANSPORT_DETAIL_HPP_
