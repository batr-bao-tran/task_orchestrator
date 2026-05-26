#include "protocol/http_transport.hpp"

#include <openssl/ssl.h>

#include <algorithm>
#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "detail/http_transport_detail.hpp"
#include "utils/logger.hpp"

namespace task_orchestrator::protocol {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using detail::configure_tls_server_name;
using detail::ignorable_tls_shutdown_error;
using detail::kAcceptPollInterval;
using detail::kOperatorDashboardSseHeartbeatInterval;
using detail::make_client_tls_context;
using detail::make_http_client_request;
using detail::make_server_tls_context;
using detail::make_transport_error_response;
using detail::parse_http_runtime_response;
using detail::resolve_expected_peer_name;
using detail::serialize_message;
using detail::TlsClientStream;
using detail::TlsServerStream;

}  // namespace

struct BeastHttpWorkflowApiServer::Impl {
  WorkflowRuntimeService& service;
  WorkflowOperatorService* operator_service = nullptr;
  WorkflowOperatorEventService* operator_event_service = nullptr;
  HttpEndpointOptions options;
  std::shared_ptr<const TlsCredentialProvider> tls_provider;
  asio::io_context accept_context{1};
  tcp::acceptor acceptor{accept_context};
  std::unique_ptr<asio::thread_pool> worker_pool;
  std::shared_ptr<ssl::context> tls_context;
  std::jthread accept_thread;
  std::atomic<bool> is_running{false};
  int bound_port = 0;
  std::string startup_error_message;
  std::shared_ptr<spdlog::logger> logger = get_logger(LogLayer::Application);
  std::mutex active_sockets_mutex;
  std::set<tcp::socket*> active_sockets;

  Impl(WorkflowRuntimeService& runtime_service,
       HttpEndpointOptions endpoint_options,
       std::shared_ptr<const TlsCredentialProvider> tls_credential_provider,
       WorkflowOperatorService* workflow_operator_service,
       WorkflowOperatorEventService* workflow_operator_event_service)
      : service(runtime_service),
        operator_service(workflow_operator_service),
        operator_event_service(workflow_operator_event_service),
        options(std::move(endpoint_options)),
        tls_provider(std::move(tls_credential_provider)) {}

  void reset_startup_state() {
    is_running.store(false);
    worker_pool.reset();
  }

  bool fail_startup(const std::string_view context, const beast::error_code& error_code) {
    if (!error_code) {
      return false;
    }
    startup_error_message = std::string(context) + ": " + error_code.message();
    reset_startup_state();
    logger->error("{}", startup_error_message);
    return true;
  }

  void start() {
    if (is_running.exchange(true)) {
      return;
    }
    startup_error_message.clear();
    if (options.use_tls) {
      const TlsServerLoadResult tls_load_result = tls_provider->load_server_credentials(options.tls);
      if (!tls_load_result.ok() ||
          !make_server_tls_context(tls_load_result.value, &tls_context, &startup_error_message)) {
        if (startup_error_message.empty()) {
          startup_error_message = tls_load_result.error_message;
        }
        is_running.store(false);
        logger->error("Failed to initialize HTTP TLS context: {}", startup_error_message);
        return;
      }
    }

    accept_context.restart();
    worker_pool = std::make_unique<asio::thread_pool>(std::max<std::size_t>(1U, options.io_threads));

    beast::error_code error_code;
    const auto bind_address = asio::ip::make_address(options.bind_address, error_code);
    if (error_code) {
      reset_startup_state();
      logger->error("Invalid HTTP bind address: {}", error_code.message());
      return;
    }

    [[maybe_unused]] const auto open_result = acceptor.open(bind_address.is_v6() ? tcp::v6() : tcp::v4(), error_code);
    if (fail_startup("Failed to open HTTP acceptor", error_code)) {
      return;
    }

    [[maybe_unused]] const auto set_option_result =
        acceptor.set_option(asio::socket_base::reuse_address(true), error_code);
    if (fail_startup("Failed to configure HTTP acceptor", error_code)) {
      return;
    }

    [[maybe_unused]] const auto bind_result =
        acceptor.bind(tcp::endpoint(bind_address, static_cast<unsigned short>(options.port)), error_code);
    if (fail_startup("Failed to bind HTTP acceptor", error_code)) {
      return;
    }

    [[maybe_unused]] const auto listen_result = acceptor.listen(asio::socket_base::max_listen_connections, error_code);
    if (fail_startup("Failed to listen on HTTP acceptor", error_code)) {
      return;
    }

    [[maybe_unused]] const auto non_blocking_result = acceptor.non_blocking(true, error_code);
    if (fail_startup("Failed to set HTTP acceptor to non-blocking mode", error_code)) {
      return;
    }

    const auto local_endpoint = acceptor.local_endpoint(error_code);
    if (fail_startup("Failed to query HTTP endpoint", error_code)) {
      return;
    }
    bound_port = static_cast<int>(local_endpoint.port());

    accept_thread = std::jthread([this]() { accept_loop(); });
    logger->info("HTTP runtime API listening on {}", endpoint());
  }

  void stop() {
    if (!is_running.exchange(false)) {
      return;
    }

    beast::error_code error_code;
    [[maybe_unused]] const auto cancel_result = acceptor.cancel(error_code);
    if (error_code) {
      logger->debug("HTTP shutdown: Failed to cancel HTTP acceptor: {}", error_code.message());
    }
    [[maybe_unused]] const auto close_result = acceptor.close(error_code);
    if (error_code) {
      logger->debug("HTTP shutdown: Failed to close HTTP acceptor: {}", error_code.message());
    }
    if (accept_thread.joinable()) {
      accept_thread.join();
    }
    if (worker_pool) {
      worker_pool->stop();
      close_active_sockets(active_sockets_mutex, active_sockets);
      worker_pool->join();
      worker_pool.reset();
    }
    tls_context.reset();
    bound_port = 0;
  }

  [[nodiscard]] std::string endpoint() const {
    return std::string(options.use_tls ? "https://" : "http://") + options.bind_address + ":" +
           std::to_string(bound_port == 0 ? options.port : bound_port);
  }

  static void register_socket(std::mutex& active_sockets_mutex,
                              std::set<tcp::socket*>& active_sockets,
                              tcp::socket* socket) {
    std::scoped_lock lock(active_sockets_mutex);
    active_sockets.insert(socket);
  }

  static void unregister_socket(std::mutex& active_sockets_mutex,
                                std::set<tcp::socket*>& active_sockets,
                                tcp::socket* socket) {
    std::scoped_lock lock(active_sockets_mutex);
    active_sockets.erase(socket);
  }

  static void close_active_sockets(std::mutex& active_sockets_mutex, std::set<tcp::socket*>& active_sockets) {
    std::scoped_lock lock(active_sockets_mutex);
    for (auto* socket : active_sockets) {
      beast::error_code ec;
      [[maybe_unused]] const auto shutdown_result = socket->shutdown(tcp::socket::shutdown_both, ec);
      [[maybe_unused]] const auto close_result = socket->close(ec);
    }
  }

  // NOLINTNEXTLINE(readability-make-member-function-const): accepts sockets via mutable Asio state.
  void accept_loop() {
    while (is_running.load()) {
      tcp::socket socket(accept_context);
      beast::error_code accept_error_code;
      [[maybe_unused]] const auto accept_result = acceptor.accept(socket, accept_error_code);
      if (accept_error_code) {
        if (accept_error_code == asio::error::would_block || accept_error_code == asio::error::try_again) {
          std::this_thread::sleep_for(kAcceptPollInterval);
          continue;
        }
        if (is_running.load()) {
          logger->warn("HTTP accept failed: {}", accept_error_code.message());
        }
        continue;
      }

      asio::post(*worker_pool, [this, socket = std::move(socket)]() mutable { handle_connection(std::move(socket)); });
    }
  }

  void handle_connection(tcp::socket socket) {
    if (!is_running.load()) {
      return;
    }
    if (options.use_tls) {
      handle_tls_connection(std::move(socket));
      return;
    }

    handle_plain_connection(std::move(socket));
  }

  template <typename Stream>
  void process_http_session(Stream& stream) {
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(options.max_body_bytes);

    beast::error_code error_code;
    http::read(stream, buffer, parser, error_code);
    if (error_code) {
      logger->warn("HTTP read failed: {}", error_code.message());
      return;
    }

    const auto& request = parser.get();
    if (detail::is_operator_dashboard_stream_request(request)) {
      process_operator_dashboard_stream(stream, request);
      return;
    }

    auto response = handle_request(request);
    http::write(stream, response, error_code);
    if (error_code) {
      logger->warn("HTTP write failed: {}", error_code.message());
    }
  }

  template <typename Stream>
  void process_operator_dashboard_stream(Stream& stream, const http::request<http::string_body>& request) {
    if (operator_service == nullptr || operator_event_service == nullptr) {
      auto response = detail::make_http_message_response(
          http::status::service_unavailable,
          detail::make_error_message<GetOperatorDashboardResponse>(
              "Operator live updates are unavailable because the control plane is not enabled."),
          request.version(),
          request.keep_alive(),
          true);
      beast::error_code error_code;
      http::write(stream, response, error_code);
      if (error_code) {
        logger->warn("HTTP write failed: {}", error_code.message());
      }
      return;
    }

    const auto target = detail::split_target(std::string_view(request.target().data(), request.target().size()));
    const auto dashboard_request = detail::make_operator_dashboard_request(target.query, request, options.use_tls);
    const std::uint64_t initial_event_id = operator_event_service->latest_dashboard_event_id();

    beast::error_code error_code;
    if (!detail::write_operator_sse_headers(stream, request.version(), &error_code)) {
      logger->warn("HTTP SSE header write failed: {}", error_code.message());
      return;
    }

    auto initial_dashboard = operator_service->get_dashboard(dashboard_request);
    if (!detail::write_operator_dashboard_sse_event(stream, initial_dashboard, initial_event_id, &error_code)) {
      if (error_code != asio::error::broken_pipe && error_code != asio::error::connection_reset) {
        logger->warn("HTTP SSE initial dashboard write failed: {}", error_code.message());
      }
      return;
    }

    std::uint64_t last_event_id = initial_event_id;
    while (is_running.load()) {
      const auto update =
          operator_event_service->wait_for_dashboard_update(last_event_id, kOperatorDashboardSseHeartbeatInterval);
      if (!update.has_value()) {
        if (!detail::write_operator_sse_comment(stream, "keepalive", &error_code)) {
          if (error_code != asio::error::broken_pipe && error_code != asio::error::connection_reset) {
            logger->warn("HTTP SSE heartbeat write failed: {}", error_code.message());
          }
          break;
        }
        continue;
      }

      last_event_id = update->event_id;
      auto dashboard_update = operator_service->get_dashboard_update(dashboard_request, *update);
      if (!detail::write_operator_dashboard_update_sse_event(stream, dashboard_update, update->event_id, &error_code)) {
        if (error_code != asio::error::broken_pipe && error_code != asio::error::connection_reset) {
          logger->warn("HTTP SSE dashboard write failed: {}", error_code.message());
        }
        break;
      }
    }

    error_code.clear();
    detail::write_operator_sse_end(stream, &error_code);
    if (error_code && error_code != asio::error::broken_pipe && error_code != asio::error::connection_reset) {
      logger->debug("HTTP SSE shutdown failed: {}", error_code.message());
    }
  }

  void handle_plain_connection(tcp::socket socket) {
    register_socket(active_sockets_mutex, active_sockets, &socket);
    process_http_session(socket);
    unregister_socket(active_sockets_mutex, active_sockets, &socket);

    beast::error_code error_code;
    [[maybe_unused]] const auto shutdown_result = socket.shutdown(tcp::socket::shutdown_both, error_code);
    if (error_code) {
      logger->debug("HTTP connection shutdown: {}", error_code.message());
    }
  }

  void handle_tls_connection(tcp::socket socket) {
    TlsServerStream tls_stream(std::move(socket), *tls_context);
    register_socket(active_sockets_mutex, active_sockets, &tls_stream.next_layer());

    beast::error_code error_code;
    tls_stream.handshake(ssl::stream_base::server, error_code);
    if (error_code) {
      unregister_socket(active_sockets_mutex, active_sockets, &tls_stream.next_layer());
      logger->warn("HTTP TLS handshake failed: {}", error_code.message());
      return;
    }

    process_http_session(tls_stream);
    unregister_socket(active_sockets_mutex, active_sockets, &tls_stream.next_layer());

    tls_stream.shutdown(error_code);
    if (error_code && !ignorable_tls_shutdown_error(error_code)) {
      logger->warn("HTTP TLS shutdown failed: {}", error_code.message());
    }
  }

  http::response<http::string_body> handle_request(const http::request<http::string_body>& request) {
    return detail::handle_http_request(service, operator_service, options.use_tls, request);
  }
};

struct BeastHttpWorkflowApiClient::Impl {
  HttpClientOptions options;
  std::shared_ptr<const TlsCredentialProvider> tls_provider;
  LoadedTlsClientConfig tls_config;
  std::shared_ptr<ssl::context> tls_context;
  std::string startup_error_message;
  std::shared_ptr<spdlog::logger> logger = get_logger(LogLayer::Application);

  Impl(HttpClientOptions client_options, std::shared_ptr<const TlsCredentialProvider> tls_credential_provider)
      : options(std::move(client_options)), tls_provider(std::move(tls_credential_provider)) {
    if (options.use_tls) {
      const TlsClientLoadResult tls_load_result = tls_provider->load_client_credentials(options.tls);
      if (!tls_load_result.ok()) {
        startup_error_message = tls_load_result.error_message;
        logger->error("Failed to initialize HTTP client TLS credentials: {}", startup_error_message);
        return;
      }
      tls_config = tls_load_result.value;
      if (!make_client_tls_context(tls_config, &tls_context, &startup_error_message)) {
        logger->error("Failed to initialize HTTP client TLS context: {}", startup_error_message);
      }
    }
  }

  RuntimeApiResponse invoke(const std::string& target, const google::protobuf::Message& request_message) const {
    if (!startup_error_message.empty()) {
      return make_transport_error_response(std::string("HTTP transport failed: ") + startup_error_message);
    }

    std::string body;
    std::string content_type;
    std::string serialization_error;
    if (!serialize_message(request_message, &body, &content_type, &serialization_error)) {
      return make_transport_error_response(std::move(serialization_error));
    }

    return options.use_tls ? invoke_tls(target, body, content_type) : invoke_plain(target, body, content_type);
  }

  [[nodiscard]] http::request<http::string_body> make_request(const std::string& target,
                                                              const std::string& body,
                                                              const std::string& content_type) const {
    return make_http_client_request(options, target, body, content_type);
  }

  [[nodiscard]] static RuntimeApiResponse parse_http_response(const http::response<http::string_body>& response) {
    return parse_http_runtime_response(response);
  }

  static RuntimeApiResponse fail_transport(const HttpClientOptions& options,
                                           const std::shared_ptr<spdlog::logger>& logger,
                                           const std::string_view context,
                                           const beast::error_code& error_code) {
    const std::string message = std::string(context) + ": " + error_code.message();
    const std::string endpoint = options.host + ":" + std::to_string(options.port);
    logger->warn("{} for {}", message, endpoint);
    return make_transport_error_response(std::string("HTTP transport failed: ") + message);
  }

  static RuntimeApiResponse fail_transport(const HttpClientOptions& options,
                                           const std::shared_ptr<spdlog::logger>& logger,
                                           const std::string& message) {
    const std::string endpoint = options.host + ":" + std::to_string(options.port);
    logger->warn("{} for {}", message, endpoint);
    return make_transport_error_response(std::string("HTTP transport failed: ") + message);
  }

  RuntimeApiResponse invoke_plain(const std::string& target,
                                  const std::string& body,
                                  const std::string& content_type) const {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    beast::tcp_stream stream(io_context);
    stream.expires_after(std::chrono::milliseconds(options.timeout_ms));

    beast::error_code error_code;
    const auto resolved_endpoints = resolver.resolve(options.host, std::to_string(options.port), error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTP resolve failed", error_code);
    }
    [[maybe_unused]] const auto connect_result = stream.connect(resolved_endpoints, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTP connect failed", error_code);
    }

    auto request = make_request(target, body, content_type);
    http::write(stream, request, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTP write failed", error_code);
    }

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTP read failed", error_code);
    }

    [[maybe_unused]] const auto shutdown_result = stream.socket().shutdown(tcp::socket::shutdown_both, error_code);
    if (error_code) {
      logger->debug("HTTP client shutdown: {}", error_code.message());
    }
    return parse_http_response(response);
  }

  RuntimeApiResponse invoke_tls(const std::string& target,
                                const std::string& body,
                                const std::string& content_type) const {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    TlsClientStream stream(io_context, *tls_context);
    beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(options.timeout_ms));

    beast::error_code error_code;
    const auto resolved_endpoints = resolver.resolve(options.host, std::to_string(options.port), error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTPS resolve failed", error_code);
    }
    [[maybe_unused]] const auto connect_result =
        beast::get_lowest_layer(stream).connect(resolved_endpoints, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTPS connect failed", error_code);
    }

    const std::string expected_peer_name = resolve_expected_peer_name(options, tls_config);
    std::string transport_error_message;
    if (!configure_tls_server_name(stream, expected_peer_name, &transport_error_message)) {
      return fail_transport(options, logger, transport_error_message);
    }
    if (tls_config.server_trust.verify_peer) {
      stream.set_verify_callback(ssl::host_name_verification(expected_peer_name));
    }
    stream.handshake(ssl::stream_base::client, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTPS handshake failed", error_code);
    }

    auto request = make_request(target, body, content_type);
    http::write(stream, request, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTPS write failed", error_code);
    }

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response, error_code);
    if (error_code) {
      return fail_transport(options, logger, "HTTPS read failed", error_code);
    }

    beast::error_code shutdown_error_code;
    stream.shutdown(shutdown_error_code);
    if (shutdown_error_code && !ignorable_tls_shutdown_error(shutdown_error_code)) {
      return fail_transport(options, logger, "HTTPS shutdown failed", shutdown_error_code);
    }
    return parse_http_response(response);
  }
};

BeastHttpWorkflowApiServer::BeastHttpWorkflowApiServer(WorkflowRuntimeService& service,
                                                       HttpEndpointOptions options,
                                                       std::shared_ptr<const TlsCredentialProvider> tls_provider,
                                                       WorkflowOperatorService* operator_service,
                                                       WorkflowOperatorEventService* operator_event_service)
    : impl_(std::make_unique<Impl>(
          service, std::move(options), std::move(tls_provider), operator_service, operator_event_service)) {}

BeastHttpWorkflowApiServer::~BeastHttpWorkflowApiServer() noexcept { stop(); }

void BeastHttpWorkflowApiServer::start() { impl_->start(); }

void BeastHttpWorkflowApiServer::stop() { impl_->stop(); }

bool BeastHttpWorkflowApiServer::running() const { return impl_->is_running.load(); }

std::string BeastHttpWorkflowApiServer::endpoint() const { return impl_->endpoint(); }

BeastHttpWorkflowApiClient::BeastHttpWorkflowApiClient(HttpClientOptions options,
                                                       std::shared_ptr<const TlsCredentialProvider> tls_provider)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(tls_provider))) {}

BeastHttpWorkflowApiClient::~BeastHttpWorkflowApiClient() noexcept = default;

RuntimeApiResponse BeastHttpWorkflowApiClient::submit(const SubmitWorkflowRequest& request) {
  return impl_->invoke(std::string(kHttpSubmitWorkflowPath), request);
}

RuntimeApiResponse BeastHttpWorkflowApiClient::reorchestrate(const ReorchestrateRequest& request) {
  return impl_->invoke(
      std::string(kHttpWorkflowsPathPrefix) + request.workflow_id() + std::string(kHttpReorchestratePathSuffix),
      request);
}

}  // namespace task_orchestrator::protocol
