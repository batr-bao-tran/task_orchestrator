#ifndef TASK_ORCHESTRATOR__PROTOCOL_SRC_DETAIL__HTTP_TRANSPORT_DETAIL_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_SRC_DETAIL__HTTP_TRANSPORT_DETAIL_HPP_

#include <openssl/ssl.h>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "protocol/http_transport.hpp"

namespace task_orchestrator::protocol::detail {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

inline constexpr int kHttpVersion11 = 11;
inline constexpr std::string_view kBearerPrefix = "Bearer ";
inline constexpr auto kAcceptPollInterval = std::chrono::milliseconds(10);
inline constexpr long kTlsContextOptions =
    ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::single_dh_use;

using TlsServerStream = beast::ssl_stream<tcp::socket>;
using TlsClientStream = beast::ssl_stream<beast::tcp_stream>;

template <typename Message>
bool parse_message(std::string_view body, Message* message, std::string* error_message) {
  if (!message->ParseFromArray(body.data(), static_cast<int>(body.size()))) {
    *error_message = "Failed to parse protobuf request body.";
    return false;
  }
  return true;
}

template <typename Message>
bool serialize_message(const Message& message,
                       std::string* body,
                       std::string* content_type,
                       std::string* error_message) {
  if (!message.SerializeToString(body)) {
    *error_message = "Failed to serialize protobuf message.";
    return false;
  }
  *content_type = std::string(kBinaryProtoContentType);
  return true;
}

template <typename Request>
void apply_transport_auth(const http::request<http::string_body>& request,
                          bool secure_transport,
                          Request* api_request) {
  pb::ClientAuthContext* auth = api_request->mutable_auth();
  if (!request[kAuthorizationHeader].empty() && auth->bearer_token().empty()) {
    const auto authorization_header_view = request[kAuthorizationHeader];
    const std::string authorization_header(authorization_header_view.data(), authorization_header_view.size());
    if (authorization_header.starts_with(kBearerPrefix)) {
      auth->set_bearer_token(authorization_header.substr(kBearerPrefix.size()));
    }
  }
  if (!request[kApiKeyHeader].empty() && auth->api_key().empty()) {
    const auto api_key_view = request[kApiKeyHeader];
    auth->set_api_key(std::string(api_key_view.data(), api_key_view.size()));
  }
  auth->set_secure_transport(secure_transport);
}

inline std::optional<std::string> extract_workflow_id_from_target(std::string_view target) {
  if (!target.starts_with(kHttpWorkflowsPathPrefix) || !target.ends_with(kHttpReorchestratePathSuffix)) {
    return std::nullopt;
  }

  const std::size_t workflow_id_offset = kHttpWorkflowsPathPrefix.size();
  const std::size_t workflow_id_size = target.size() - workflow_id_offset - kHttpReorchestratePathSuffix.size();
  if (workflow_id_size == 0U) {
    return std::nullopt;
  }
  return std::string(target.substr(workflow_id_offset, workflow_id_size));
}

inline RuntimeApiResponse make_transport_error_response(std::string error_message) {
  RuntimeApiResponse response;
  response.set_ok(false);
  response.set_error_message(std::move(error_message));
  response.mutable_result()->set_ok(false);
  response.mutable_result()->set_error_message(response.error_message());
  return response;
}

inline bool assign_error_from_code(const beast::error_code& error_code,
                                   const std::string_view context,
                                   std::string* error_message) {
  if (!error_code) {
    return true;
  }
  *error_message = std::string(context) + ": " + error_code.message();
  return false;
}

template <typename Operation>
bool run_operation(const std::string_view context, std::string* error_message, Operation&& operation) {
  beast::error_code error_code;
  if constexpr (std::is_void_v<std::invoke_result_t<Operation, beast::error_code&>>) {
    std::forward<Operation>(operation)(error_code);
  } else {
    [[maybe_unused]] const auto operation_result = std::forward<Operation>(operation)(error_code);
  }
  return assign_error_from_code(error_code, context, error_message);
}

inline bool configure_minimum_tls_version(ssl::context& context, std::string* error_message) {
  if (SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION) != 1) {
    *error_message = "Failed to configure minimum TLS protocol version.";
    return false;
  }
  return true;
}

inline bool configure_tls_identity(ssl::context& context,
                                   const LoadedTlsIdentityConfig& identity,
                                   const std::string_view owner_label,
                                   std::string* error_message) {
  if (!identity.private_key_password.empty()) {
    const std::string private_key_password = identity.private_key_password;
    context.set_password_callback([private_key_password](std::size_t, ssl::context::password_purpose) {
      return std::string(private_key_password);
    });
  }

  if (!run_operation(
          std::string(owner_label) + " TLS certificate chain", error_message, [&](beast::error_code& error_code) {
            return context.use_certificate_chain(asio::buffer(identity.certificate_chain_pem), error_code);
          })) {
    return false;
  }

  return run_operation(
      std::string(owner_label) + " TLS private key", error_message, [&](beast::error_code& error_code) {
        return context.use_private_key(asio::buffer(identity.private_key_pem), ssl::context::pem, error_code);
      });
}

inline bool configure_tls_trust(ssl::context& context,
                                const LoadedTlsTrustConfig& trust,
                                const std::string_view owner_label,
                                std::string* error_message) {
  if (trust.use_system_default_roots) {
    if (!run_operation(std::string(owner_label) + " TLS system trust store",
                       error_message,
                       [&](beast::error_code& error_code) { return context.set_default_verify_paths(error_code); })) {
      return false;
    }
  }

  if (!trust.root_certificates_pem.empty()) {
    if (!run_operation(
            std::string(owner_label) + " TLS root certificates", error_message, [&](beast::error_code& error_code) {
              return context.add_certificate_authority(asio::buffer(trust.root_certificates_pem), error_code);
            })) {
      return false;
    }
  }
  return true;
}

inline bool make_server_tls_context(const LoadedTlsServerConfig& tls_config,
                                    std::shared_ptr<ssl::context>* context,
                                    std::string* error_message) {
  *context = std::make_shared<ssl::context>(ssl::context::tls_server);

  if (!run_operation(
          "HTTP TLS context options",
          error_message,
          [&](beast::error_code& error_code) { return (*context)->set_options(kTlsContextOptions, error_code); }) ||
      !configure_minimum_tls_version(*(*context), error_message) ||
      !configure_tls_identity(*(*context), tls_config.identity, "HTTP server", error_message) ||
      !configure_tls_trust(*(*context), tls_config.client_trust, "HTTP server", error_message)) {
    context->reset();
    return false;
  }

  if (tls_config.require_client_certificate) {
    (*context)->set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
  } else {
    (*context)->set_verify_mode(ssl::verify_none);
  }

  return true;
}

inline bool make_client_tls_context(const LoadedTlsClientConfig& tls_config,
                                    std::shared_ptr<ssl::context>* context,
                                    std::string* error_message) {
  *context = std::make_shared<ssl::context>(ssl::context::tls_client);

  if (!run_operation(
          "HTTP TLS context options",
          error_message,
          [&](beast::error_code& error_code) { return (*context)->set_options(kTlsContextOptions, error_code); }) ||
      !configure_minimum_tls_version(*(*context), error_message) ||
      !configure_tls_trust(*(*context), tls_config.server_trust, "HTTP client", error_message)) {
    context->reset();
    return false;
  }
  if (tls_config.identity.configured()) {
    if (!configure_tls_identity(*(*context), tls_config.identity, "HTTP client", error_message)) {
      context->reset();
      return false;
    }
  }

  (*context)->set_verify_mode(tls_config.server_trust.verify_peer ? ssl::verify_peer : ssl::verify_none);
  return true;
}

inline std::string resolve_expected_peer_name(const HttpClientOptions& options,
                                              const LoadedTlsClientConfig& tls_config) {
  return tls_config.server_trust.expected_peer_name.empty() ? options.host : tls_config.server_trust.expected_peer_name;
}

inline bool configure_tls_server_name(TlsClientStream& stream,
                                      const std::string& expected_peer_name,
                                      std::string* error_message) {
  if (expected_peer_name.empty()) {
    return true;
  }
  if (SSL_set_tlsext_host_name(stream.native_handle(), expected_peer_name.c_str()) != 1) {
    *error_message = "Failed to configure HTTP TLS server name indication.";
    return false;
  }
  return true;
}

inline bool ignorable_tls_shutdown_error(const beast::error_code& error_code) {
  return error_code == asio::error::eof || error_code == ssl::error::stream_truncated;
}

inline http::response<http::string_body> make_http_error(http::status status,
                                                         std::string error_message,
                                                         unsigned version,
                                                         bool keep_alive) {
  const RuntimeApiResponse response = make_transport_error_response(std::move(error_message));
  std::string body;
  std::string content_type;
  std::string serialization_error;
  if (!serialize_message(response, &body, &content_type, &serialization_error)) {
    body = serialization_error;
    content_type = "text/plain";
  }

  http::response<http::string_body> http_response(status, version);
  http_response.set(http::field::server, "task_orchestrator/http");
  http_response.set(http::field::content_type, content_type);
  http_response.keep_alive(keep_alive);
  http_response.body() = std::move(body);
  http_response.prepare_payload();
  return http_response;
}

inline http::response<http::string_body> make_http_success_response(const RuntimeApiResponse& response,
                                                                    unsigned version,
                                                                    bool keep_alive) {
  std::string body;
  std::string content_type;
  std::string error_message;
  if (!serialize_message(response, &body, &content_type, &error_message)) {
    return make_http_error(http::status::internal_server_error, std::move(error_message), version, keep_alive);
  }

  http::response<http::string_body> http_response(http::status::ok, version);
  http_response.set(http::field::server, "task_orchestrator/http");
  http_response.set(http::field::content_type, content_type);
  http_response.keep_alive(keep_alive);
  http_response.body() = std::move(body);
  http_response.prepare_payload();
  return http_response;
}

inline http::response<http::string_body> handle_http_request(WorkflowRuntimeService& service,
                                                             bool secure_transport,
                                                             const http::request<http::string_body>& request) {
  if (request.method() != http::verb::post) {
    return make_http_error(http::status::method_not_allowed, "Only POST is supported.", request.version(), false);
  }

  const std::string_view request_target = request.target();
  if (request_target == kHttpSubmitWorkflowPath) {
    SubmitWorkflowRequest submit_request;
    std::string parse_error;
    if (!parse_message(request.body(), &submit_request, &parse_error)) {
      return make_http_error(http::status::bad_request, std::move(parse_error), request.version(), false);
    }
    apply_transport_auth(request, secure_transport, &submit_request);
    return make_http_success_response(service.submit_workflow(submit_request), request.version(), request.keep_alive());
  }

  if (const auto workflow_id = extract_workflow_id_from_target(request_target); workflow_id.has_value()) {
    ReorchestrateRequest reorchestrate_request;
    std::string parse_error;
    if (!parse_message(request.body(), &reorchestrate_request, &parse_error)) {
      return make_http_error(http::status::bad_request, std::move(parse_error), request.version(), false);
    }
    if (!reorchestrate_request.workflow_id().empty() && reorchestrate_request.workflow_id() != *workflow_id) {
      return make_http_error(http::status::bad_request,
                             "workflow_id in path does not match workflow_id in body.",
                             request.version(),
                             false);
    }
    reorchestrate_request.set_workflow_id(*workflow_id);
    apply_transport_auth(request, secure_transport, &reorchestrate_request);
    return make_http_success_response(
        service.reorchestrate(reorchestrate_request), request.version(), request.keep_alive());
  }

  return make_http_error(http::status::not_found, "Unsupported route.", request.version(), false);
}

inline http::request<http::string_body> make_http_client_request(const HttpClientOptions& options,
                                                                 const std::string& target,
                                                                 std::string body,
                                                                 std::string_view content_type) {
  http::request<http::string_body> request(http::verb::post, target, kHttpVersion11);
  request.set(http::field::host, options.host);
  request.set(http::field::user_agent, "task_orchestrator/http-client");
  request.set(http::field::content_type, content_type);
  request.set(http::field::accept, std::string(kBinaryProtoContentType));
  if (!options.bearer_token.empty()) {
    request.set(kAuthorizationHeader, std::string(kBearerPrefix) + options.bearer_token);
  }
  if (!options.api_key.empty()) {
    request.set(kApiKeyHeader, options.api_key);
  }
  request.body() = std::move(body);
  request.prepare_payload();
  return request;
}

inline RuntimeApiResponse parse_http_runtime_response(const http::response<http::string_body>& response) {
  RuntimeApiResponse api_response;
  std::string parse_error;
  if (!parse_message(response.body(), &api_response, &parse_error)) {
    return make_transport_error_response(std::move(parse_error));
  }
  return api_response;
}

}  // namespace task_orchestrator::protocol::detail

#endif  // TASK_ORCHESTRATOR__PROTOCOL_SRC_DETAIL__HTTP_TRANSPORT_DETAIL_HPP_
