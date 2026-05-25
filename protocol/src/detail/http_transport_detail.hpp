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
#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "google/protobuf/util/json_util.h"
#include "protocol/http_transport.hpp"
#include "protocol/operator_api.hpp"

namespace task_orchestrator::protocol::detail {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

inline constexpr int kHttpVersion11 = 11;
inline constexpr std::string_view kBearerPrefix = "Bearer ";
inline constexpr std::string_view kJsonContentType = "application/json";
inline constexpr std::string_view kTextEventStreamContentType = "text/event-stream";
inline constexpr auto kAcceptPollInterval = std::chrono::milliseconds(10);
inline constexpr auto kOperatorDashboardSseHeartbeatInterval = std::chrono::seconds(2);
inline constexpr std::uint64_t kOperatorDashboardSseRetryMillis = 2000;
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

template <typename Message>
bool parse_json_message(std::string_view body, Message* message, std::string* error_message) {
  const auto status = google::protobuf::util::JsonStringToMessage(std::string(body),
                                                                  message,
                                                                  google::protobuf::util::JsonParseOptions{
                                                                      .ignore_unknown_fields = false,
                                                                  });
  if (!status.ok()) {
    *error_message = "Failed to parse JSON request body.";
    return false;
  }
  return true;
}

template <typename Message>
bool serialize_json_message(const Message& message,
                            std::string* body,
                            std::string* content_type,
                            std::string* error_message) {
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = false;
  options.preserve_proto_field_names = false;
  const auto status = google::protobuf::util::MessageToJsonString(message, body, options);
  if (!status.ok()) {
    *error_message = "Failed to serialize JSON response body.";
    return false;
  }
  *content_type = std::string(kJsonContentType);
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

struct SplitTarget {
  std::string_view path;
  std::string_view query;
};

struct OperatorTaskRoute {
  std::string workflow_id;
  std::string task_id;
};

inline SplitTarget split_target(std::string_view target) {
  const std::size_t query_separator = target.find('?');
  if (query_separator == std::string_view::npos) {
    return {.path = target, .query = {}};
  }
  return {
      .path = target.substr(0, query_separator),
      .query = target.substr(query_separator + 1),
  };
}

inline std::string decode_query_component(std::string_view value) {
  auto hex_digit = [](const char digit) -> int {
    if (digit >= '0' && digit <= '9') {
      return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
      return 10 + digit - 'a';
    }
    if (digit >= 'A' && digit <= 'F') {
      return 10 + digit - 'A';
    }
    return -1;
  };

  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '+') {
      decoded.push_back(' ');
      continue;
    }
    if (value[index] == '%' && index + 2 < value.size()) {
      const int high = hex_digit(value[index + 1]);
      const int low = hex_digit(value[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        continue;
      }
    }
    decoded.push_back(value[index]);
  }
  return decoded;
}

inline std::optional<std::string> query_value(std::string_view query,
                                              std::string_view snake_case_key,
                                              std::string_view camel_case_key = {}) {
  std::size_t start = 0;
  while (start <= query.size()) {
    const std::size_t end = query.find('&', start);
    const std::string_view pair =
        query.substr(start, end == std::string_view::npos ? query.size() - start : end - start);
    const std::size_t equals = pair.find('=');
    const std::string_view key = pair.substr(0, equals);
    if (key == snake_case_key || (!camel_case_key.empty() && key == camel_case_key)) {
      const std::string_view value = equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1);
      return decode_query_component(value);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return std::nullopt;
}

inline std::int32_t query_int32(std::string_view query,
                                std::string_view snake_case_key,
                                std::string_view camel_case_key = {}) {
  const auto value = query_value(query, snake_case_key, camel_case_key);
  if (!value.has_value() || value->empty()) {
    return 0;
  }
  try {
    return static_cast<std::int32_t>(std::stoi(*value));
  } catch (const std::exception&) {
    return 0;
  }
}

inline GetOperatorDashboardRequest make_operator_dashboard_request(std::string_view request_query,
                                                                   const http::request<http::string_body>& request,
                                                                   const bool secure_transport) {
  GetOperatorDashboardRequest dashboard_request;
  if (const auto selected_workflow_id = query_value(request_query, "selected_workflow_id", "selectedWorkflowId");
      selected_workflow_id.has_value()) {
    dashboard_request.set_selected_workflow_id(*selected_workflow_id);
  }
  if (const auto workflow_query = query_value(request_query, "workflow_query", "workflowQuery");
      workflow_query.has_value()) {
    dashboard_request.set_workflow_query(*workflow_query);
  }
  dashboard_request.set_workflow_page_size(query_int32(request_query, "workflow_page_size", "workflowPageSize"));
  dashboard_request.set_max_events(query_int32(request_query, "max_events", "maxEvents"));
  dashboard_request.set_max_plan_versions(query_int32(request_query, "max_plan_versions", "maxPlanVersions"));
  dashboard_request.set_max_audit_entries(query_int32(request_query, "max_audit_entries", "maxAuditEntries"));
  apply_transport_auth(request, secure_transport, &dashboard_request);
  return dashboard_request;
}

inline bool is_operator_dashboard_stream_request(const http::request<http::string_body>& request) {
  const auto target = split_target(std::string_view(request.target().data(), request.target().size()));
  return request.method() == http::verb::get && target.path == kHttpOperatorDashboardStreamPath;
}

inline std::optional<std::string> extract_operator_workflow_id(std::string_view path, std::string_view suffix) {
  if (!path.starts_with(kHttpOperatorWorkflowsPathPrefix) || !path.ends_with(suffix)) {
    return std::nullopt;
  }
  const std::size_t workflow_id_offset = kHttpOperatorWorkflowsPathPrefix.size();
  const std::size_t workflow_id_size = path.size() - workflow_id_offset - suffix.size();
  if (workflow_id_size == 0U) {
    return std::nullopt;
  }
  const std::string_view workflow_id = path.substr(workflow_id_offset, workflow_id_size);
  if (absl::StrContains(workflow_id, '/')) {
    return std::nullopt;
  }
  return std::string(workflow_id);
}

inline std::optional<std::string> extract_operator_task_workflow_id(std::string_view path) {
  if (!path.starts_with(kHttpOperatorWorkflowsPathPrefix) || !path.ends_with(kHttpOperatorTasksSuffix)) {
    return std::nullopt;
  }
  const std::size_t workflow_id_offset = kHttpOperatorWorkflowsPathPrefix.size();
  const std::size_t workflow_id_size = path.size() - workflow_id_offset - kHttpOperatorTasksSuffix.size();
  if (workflow_id_size == 0U) {
    return std::nullopt;
  }
  const std::string_view workflow_id = path.substr(workflow_id_offset, workflow_id_size);
  if (absl::StrContains(workflow_id, '/')) {
    return std::nullopt;
  }
  return std::string(workflow_id);
}

inline std::optional<OperatorTaskRoute> extract_operator_task_delete_route(std::string_view path) {
  if (!path.starts_with(kHttpOperatorWorkflowsPathPrefix) || !path.ends_with(kHttpOperatorDeleteTaskSuffix)) {
    return std::nullopt;
  }

  const std::string_view stripped =
      path.substr(kHttpOperatorWorkflowsPathPrefix.size(),
                  path.size() - kHttpOperatorWorkflowsPathPrefix.size() - kHttpOperatorDeleteTaskSuffix.size());
  const std::size_t separator = stripped.find("/tasks/");
  if (separator == std::string_view::npos) {
    return std::nullopt;
  }

  const std::string_view workflow_id = stripped.substr(0, separator);
  const std::string_view task_id = stripped.substr(separator + std::string_view("/tasks/").size());
  if (workflow_id.empty() || task_id.empty()) {
    return std::nullopt;
  }

  return OperatorTaskRoute{
      .workflow_id = std::string(workflow_id),
      .task_id = std::string(task_id),
  };
}

template <typename Message>
Message make_error_message(const std::string& error_message) {
  Message response;
  response.set_ok(false);
  response.set_error_message(error_message);
  return response;
}

template <typename Message>
http::response<http::string_body> make_http_message_response(
    http::status status, const Message& message, unsigned version, bool keep_alive, bool json_response) {
  std::string body;
  std::string content_type;
  std::string serialization_error;
  const bool serialized = json_response ? serialize_json_message(message, &body, &content_type, &serialization_error)
                                        : serialize_message(message, &body, &content_type, &serialization_error);
  if (!serialized) {
    http::response<http::string_body> fallback(http::status::internal_server_error, version);
    fallback.set(http::field::server, "task_orchestrator/http");
    fallback.set(http::field::content_type, "text/plain");
    fallback.keep_alive(keep_alive);
    fallback.body() = std::move(serialization_error);
    fallback.prepare_payload();
    return fallback;
  }

  http::response<http::string_body> http_response(status, version);
  http_response.set(http::field::server, "task_orchestrator/http");
  http_response.set(http::field::content_type, content_type);
  http_response.keep_alive(keep_alive);
  http_response.body() = std::move(body);
  http_response.prepare_payload();
  return http_response;
}

template <typename Stream>
bool write_operator_sse_headers(Stream& stream, const unsigned version, beast::error_code* error_code) {
  http::response<http::empty_body> response(http::status::ok, version);
  response.set(http::field::server, "task_orchestrator/http");
  response.set(http::field::content_type, std::string(kTextEventStreamContentType));
  response.set(http::field::cache_control, "no-cache, no-transform");
  response.set(http::field::connection, "keep-alive");
  response.set("x-accel-buffering", "no");
  response.chunked(true);
  response.keep_alive(true);

  http::response_serializer<http::empty_body> serializer(response);
  http::write_header(stream, serializer, *error_code);
  return !(*error_code);
}

template <typename Stream>
bool write_operator_sse_payload(Stream& stream, const std::string& payload, beast::error_code* error_code) {
  std::ostringstream encoded_payload;
  encoded_payload << std::hex << payload.size() << "\r\n" << payload << "\r\n";
  const std::string chunk = encoded_payload.str();
  asio::write(stream, asio::buffer(chunk), *error_code);
  return !(*error_code);
}

template <typename Message>
std::string make_operator_sse_json_payload(const Message& message,
                                           const std::string_view event_name,
                                           const std::uint64_t event_id) {
  std::string body;
  std::string content_type;
  std::string error_message;
  if (!serialize_json_message(message, &body, &content_type, &error_message)) {
    body = std::string(R"({"ok":false,"errorMessage":")") + error_message + R"("})";
  }

  std::string payload;
  payload.reserve(body.size() + event_name.size() + 64);
  payload += "id: ";
  payload += std::to_string(event_id);
  payload += "\n";
  payload += "event: ";
  payload += event_name;
  payload += "\n";
  payload += "retry: ";
  payload += std::to_string(kOperatorDashboardSseRetryMillis);
  payload += "\n";
  payload += "data: ";
  payload += body;
  payload += "\n\n";
  return payload;
}

inline std::string make_operator_dashboard_sse_payload(const GetOperatorDashboardResponse& response,
                                                       const std::uint64_t event_id) {
  return make_operator_sse_json_payload(response, "dashboard", event_id);
}

inline std::string make_operator_dashboard_update_sse_payload(const OperatorDashboardUpdate& update,
                                                              const std::uint64_t event_id) {
  return make_operator_sse_json_payload(update, "dashboard-update", event_id);
}

template <typename Stream>
bool write_operator_dashboard_sse_event(Stream& stream,
                                        const GetOperatorDashboardResponse& response,
                                        const std::uint64_t event_id,
                                        beast::error_code* error_code) {
  return write_operator_sse_payload(stream, make_operator_dashboard_sse_payload(response, event_id), error_code);
}

template <typename Stream>
bool write_operator_dashboard_update_sse_event(Stream& stream,
                                               const OperatorDashboardUpdate& update,
                                               const std::uint64_t event_id,
                                               beast::error_code* error_code) {
  return write_operator_sse_payload(stream, make_operator_dashboard_update_sse_payload(update, event_id), error_code);
}

template <typename Stream>
bool write_operator_sse_comment(Stream& stream, std::string_view comment, beast::error_code* error_code) {
  std::string payload = ": ";
  payload += comment;
  payload += "\n\n";
  return write_operator_sse_payload(stream, std::move(payload), error_code);
}

template <typename Stream>
bool write_operator_sse_end(Stream& stream, beast::error_code* error_code) {
  static constexpr std::string_view kChunkTerminator = "0\r\n\r\n";
  asio::write(stream, asio::buffer(kChunkTerminator.data(), kChunkTerminator.size()), *error_code);
  return !(*error_code);
}

inline RuntimeApiResponse make_transport_error_response(const std::string& error_message) {
  RuntimeApiResponse response;
  response.set_ok(false);
  response.set_error_message(error_message);
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

  (*context)->set_verify_mode(
      tls_config.require_client_certificate ? ssl::verify_peer | ssl::verify_fail_if_no_peer_cert : ssl::verify_none);

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
                                                         const std::string& error_message,
                                                         unsigned version,
                                                         bool keep_alive) {
  const RuntimeApiResponse response = make_transport_error_response(error_message);
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
  return make_http_message_response(http::status::ok, response, version, keep_alive, false);
}

inline http::response<http::string_body> handle_operator_http_request(WorkflowOperatorService* operator_service,
                                                                      bool secure_transport,
                                                                      const http::request<http::string_body>& request,
                                                                      std::string_view request_path,
                                                                      std::string_view request_query) {
  if (operator_service == nullptr) {
    return make_http_message_response(http::status::service_unavailable,
                                      make_error_message<GetOperatorDashboardResponse>(
                                          "Operator API is unavailable because the control plane is not enabled."),
                                      request.version(),
                                      request.keep_alive(),
                                      true);
  }

  if (request.method() == http::verb::get && request_path == kHttpOperatorDashboardPath) {
    const GetOperatorDashboardRequest dashboard_request =
        make_operator_dashboard_request(request_query, request, secure_transport);
    return make_http_message_response(http::status::ok,
                                      operator_service->get_dashboard(dashboard_request),
                                      request.version(),
                                      request.keep_alive(),
                                      true);
  }

  if (request.method() == http::verb::post && request_path == kHttpOperatorWorkflowsPath) {
    UpsertOperatorWorkflowRequest workflow_request;
    std::string parse_error;
    if (!parse_json_message(request.body(), &workflow_request, &parse_error)) {
      return make_http_message_response(http::status::bad_request,
                                        make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                        request.version(),
                                        request.keep_alive(),
                                        true);
    }
    apply_transport_auth(request, secure_transport, &workflow_request);
    return make_http_message_response(http::status::ok,
                                      operator_service->upsert_workflow(workflow_request),
                                      request.version(),
                                      request.keep_alive(),
                                      true);
  }

  if (request.method() == http::verb::post) {
    if (const auto workflow_id = extract_operator_task_workflow_id(request_path); workflow_id.has_value()) {
      UpsertOperatorTaskRequest task_request;
      std::string parse_error;
      if (!parse_json_message(request.body(), &task_request, &parse_error)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      if (!task_request.workflow_id().empty() && task_request.workflow_id() != *workflow_id) {
        return make_http_message_response(
            http::status::bad_request,
            make_error_message<OperatorMutationResponse>("workflow_id in path does not match workflow_id in body."),
            request.version(),
            request.keep_alive(),
            true);
      }
      task_request.set_workflow_id(*workflow_id);
      apply_transport_auth(request, secure_transport, &task_request);
      return make_http_message_response(
          http::status::ok, operator_service->upsert_task(task_request), request.version(), request.keep_alive(), true);
    }

    if (const auto delete_route = extract_operator_task_delete_route(request_path); delete_route.has_value()) {
      DeleteOperatorTaskRequest delete_request;
      std::string parse_error;
      if (!request.body().empty() && !parse_json_message(request.body(), &delete_request, &parse_error)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      if ((!delete_request.workflow_id().empty() && delete_request.workflow_id() != delete_route->workflow_id) ||
          (!delete_request.task_id().empty() && delete_request.task_id() != delete_route->task_id)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(
                                              "Workflow or task id in path does not match the JSON request body."),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      delete_request.set_workflow_id(delete_route->workflow_id);
      delete_request.set_task_id(delete_route->task_id);
      apply_transport_auth(request, secure_transport, &delete_request);
      return make_http_message_response(http::status::ok,
                                        operator_service->delete_task(delete_request),
                                        request.version(),
                                        request.keep_alive(),
                                        true);
    }

    if (const auto workflow_id = extract_operator_workflow_id(request_path, kHttpOperatorPauseSuffix);
        workflow_id.has_value()) {
      OperatorWorkflowActionRequest action_request;
      std::string parse_error;
      if (!request.body().empty() && !parse_json_message(request.body(), &action_request, &parse_error)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      if (!action_request.workflow_id().empty() && action_request.workflow_id() != *workflow_id) {
        return make_http_message_response(
            http::status::bad_request,
            make_error_message<OperatorMutationResponse>("workflow_id in path does not match workflow_id in body."),
            request.version(),
            request.keep_alive(),
            true);
      }
      action_request.set_workflow_id(*workflow_id);
      apply_transport_auth(request, secure_transport, &action_request);
      return make_http_message_response(http::status::ok,
                                        operator_service->pause_workflow(action_request),
                                        request.version(),
                                        request.keep_alive(),
                                        true);
    }

    if (const auto workflow_id = extract_operator_workflow_id(request_path, kHttpOperatorResumeSuffix);
        workflow_id.has_value()) {
      OperatorWorkflowActionRequest action_request;
      std::string parse_error;
      if (!request.body().empty() && !parse_json_message(request.body(), &action_request, &parse_error)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      if (!action_request.workflow_id().empty() && action_request.workflow_id() != *workflow_id) {
        return make_http_message_response(
            http::status::bad_request,
            make_error_message<OperatorMutationResponse>("workflow_id in path does not match workflow_id in body."),
            request.version(),
            request.keep_alive(),
            true);
      }
      action_request.set_workflow_id(*workflow_id);
      apply_transport_auth(request, secure_transport, &action_request);
      return make_http_message_response(http::status::ok,
                                        operator_service->resume_workflow(action_request),
                                        request.version(),
                                        request.keep_alive(),
                                        true);
    }

    if (const auto workflow_id = extract_operator_workflow_id(request_path, kHttpOperatorCancelSuffix);
        workflow_id.has_value()) {
      OperatorWorkflowActionRequest action_request;
      std::string parse_error;
      if (!request.body().empty() && !parse_json_message(request.body(), &action_request, &parse_error)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      if (!action_request.workflow_id().empty() && action_request.workflow_id() != *workflow_id) {
        return make_http_message_response(
            http::status::bad_request,
            make_error_message<OperatorMutationResponse>("workflow_id in path does not match workflow_id in body."),
            request.version(),
            request.keep_alive(),
            true);
      }
      action_request.set_workflow_id(*workflow_id);
      apply_transport_auth(request, secure_transport, &action_request);
      return make_http_message_response(http::status::ok,
                                        operator_service->cancel_workflow(action_request),
                                        request.version(),
                                        request.keep_alive(),
                                        true);
    }

    if (const auto workflow_id = extract_operator_workflow_id(request_path, kHttpOperatorManualInterventionSuffix);
        workflow_id.has_value()) {
      ManualInterventionRequest intervention_request;
      std::string parse_error;
      if (!parse_json_message(request.body(), &intervention_request, &parse_error)) {
        return make_http_message_response(http::status::bad_request,
                                          make_error_message<OperatorMutationResponse>(std::move(parse_error)),
                                          request.version(),
                                          request.keep_alive(),
                                          true);
      }
      if (!intervention_request.workflow_id().empty() && intervention_request.workflow_id() != *workflow_id) {
        return make_http_message_response(
            http::status::bad_request,
            make_error_message<OperatorMutationResponse>("workflow_id in path does not match workflow_id in body."),
            request.version(),
            request.keep_alive(),
            true);
      }
      intervention_request.set_workflow_id(*workflow_id);
      apply_transport_auth(request, secure_transport, &intervention_request);
      return make_http_message_response(http::status::ok,
                                        operator_service->apply_manual_intervention(intervention_request),
                                        request.version(),
                                        request.keep_alive(),
                                        true);
    }
  }

  return make_http_message_response(http::status::not_found,
                                    make_error_message<GetOperatorDashboardResponse>("Unsupported operator route."),
                                    request.version(),
                                    request.keep_alive(),
                                    true);
}

inline http::response<http::string_body> handle_http_request(WorkflowRuntimeService& service,
                                                             WorkflowOperatorService* operator_service,
                                                             bool secure_transport,
                                                             const http::request<http::string_body>& request) {
  const SplitTarget target = split_target(request.target());
  if (target.path.starts_with("/v1/operator")) {
    return handle_operator_http_request(operator_service, secure_transport, request, target.path, target.query);
  }

  if (request.method() != http::verb::post) {
    return make_http_error(http::status::method_not_allowed, "Only POST is supported.", request.version(), false);
  }

  const std::string_view request_target = target.path;
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
                                                                 const std::string& body,
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
  request.body() = body;
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
