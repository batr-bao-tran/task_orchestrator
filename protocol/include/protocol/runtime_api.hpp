#ifndef TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__RUNTIME_API_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__RUNTIME_API_HPP_

#include <cstddef>
#include <future>
#include <string>
#include <string_view>
#include <utility>

#include "task_orchestration_service/task_orchestration.pb.h"
#include "utils/generator.hpp"

namespace task_orchestrator::protocol {

namespace pb = ::task_orchestrator::protocol::v1;

inline constexpr const char* kDefaultLoopbackAddress = "127.0.0.1";
inline constexpr int kDefaultHttpPort = 8080;
inline constexpr int kDefaultGrpcPort = 9090;
inline constexpr std::size_t kDefaultIoThreads = 2;
inline constexpr std::size_t kDefaultMaxHttpBodyBytes = 1U << 20;
inline constexpr int kDefaultRequestTimeoutMs = 10000;
inline constexpr std::string_view kGrpcSubmitWorkflowMethod =
    "/task_orchestrator.protocol.v1.WorkflowRuntimeApi/SubmitWorkflow";
inline constexpr std::string_view kGrpcReorchestrateMethod =
    "/task_orchestrator.protocol.v1.WorkflowRuntimeApi/Reorchestrate";
inline constexpr std::string_view kGrpcStreamSubmitWorkflowMethod =
    "/task_orchestrator.protocol.v1.WorkflowRuntimeApi/StreamSubmitWorkflow";
inline constexpr std::string_view kGrpcStreamReorchestrateMethod =
    "/task_orchestrator.protocol.v1.WorkflowRuntimeApi/StreamReorchestrate";
inline constexpr std::string_view kHttpSubmitWorkflowPath = "/v1/workflows:submit";
inline constexpr std::string_view kHttpWorkflowsPathPrefix = "/v1/workflows/";
inline constexpr std::string_view kHttpReorchestratePathSuffix = ":reorchestrate";
inline constexpr std::string_view kBinaryProtoContentType = "application/x-protobuf";
inline constexpr std::string_view kAuthorizationHeader = "authorization";
inline constexpr std::string_view kApiKeyHeader = "x-api-key";

using SubmitWorkflowRequest = pb::SubmitWorkflowRequest;
using ReorchestrateRequest = pb::ReorchestrateRequest;
using RuntimeApiResponse = pb::RuntimeApiResponse;
using WorkflowEvent = pb::WorkflowEvent;
using WorkflowEventStream = task_orchestrator::Generator<WorkflowEvent>;

enum class AuthMode {
  None,
  BearerToken,
  ApiKey,
};

enum class TlsDataSourceKind {
  None,
  FilePath,
  InlinePem,
};

struct TlsDataSource {
  TlsDataSourceKind kind = TlsDataSourceKind::None;
  std::string value;

  [[nodiscard]] bool configured() const { return kind != TlsDataSourceKind::None && !value.empty(); }

  static TlsDataSource from_file(std::string file_path) {
    return TlsDataSource{.kind = TlsDataSourceKind::FilePath, .value = std::move(file_path)};
  }

  static TlsDataSource from_inline_pem(std::string pem) {
    return TlsDataSource{.kind = TlsDataSourceKind::InlinePem, .value = std::move(pem)};
  }
};

struct TlsIdentityConfig {
  TlsDataSource certificate_chain;
  TlsDataSource private_key;
  TlsDataSource private_key_password;

  [[nodiscard]] bool configured() const {
    return certificate_chain.configured() || private_key.configured() || private_key_password.configured();
  }
};

struct TlsTrustConfig {
  TlsDataSource root_certificates;
  bool use_system_default_roots = true;
  bool verify_peer = true;
  std::string expected_peer_name;
};

struct TlsServerConfig {
  TlsIdentityConfig identity;
  TlsTrustConfig client_trust;
  bool require_client_certificate = false;
};

struct TlsClientConfig {
  TlsIdentityConfig identity;
  TlsTrustConfig server_trust;
};

struct SecurityConfig {
  AuthMode mode = AuthMode::None;
  std::string expected_credential;
  bool require_secure_transport = false;
};

/** @brief HTTP server transport configuration for the runtime API. */
struct HttpEndpointOptions {
  std::string bind_address = kDefaultLoopbackAddress;
  int port = kDefaultHttpPort;
  bool use_tls = false;
  TlsServerConfig tls;
  std::size_t io_threads = kDefaultIoThreads;
  std::size_t max_body_bytes = kDefaultMaxHttpBodyBytes;
};

/** @brief HTTP client transport configuration for the runtime API. */
struct HttpClientOptions {
  std::string host = kDefaultLoopbackAddress;
  int port = kDefaultHttpPort;
  bool use_tls = false;
  TlsClientConfig tls;
  int timeout_ms = kDefaultRequestTimeoutMs;
  std::string bearer_token;
  std::string api_key;
};

/** @brief gRPC server transport configuration for the runtime API. */
struct GrpcEndpointOptions {
  std::string bind_address = kDefaultLoopbackAddress;
  int port = kDefaultGrpcPort;
  bool use_tls = false;
  TlsServerConfig tls;
  int max_receive_message_bytes = static_cast<int>(kDefaultMaxHttpBodyBytes);
  int max_send_message_bytes = static_cast<int>(kDefaultMaxHttpBodyBytes);
};

/** @brief gRPC client transport configuration for the runtime API. */
struct GrpcClientOptions {
  std::string host = kDefaultLoopbackAddress;
  int port = kDefaultGrpcPort;
  bool use_tls = false;
  TlsClientConfig tls;
  int deadline_ms = kDefaultRequestTimeoutMs;
  std::string bearer_token;
  std::string api_key;
};

/** @brief Runtime service contract used by the HTTP and gRPC transports. */
class WorkflowRuntimeService {
 public:
  virtual ~WorkflowRuntimeService() noexcept = default;

  /** @brief Submit a workflow and return the final orchestration result. */
  virtual RuntimeApiResponse submit_workflow(const SubmitWorkflowRequest& request) = 0;
  /** @brief Re-run planning for an existing workflow session. */
  virtual RuntimeApiResponse reorchestrate(const ReorchestrateRequest& request) = 0;
  /** @brief Submit a workflow on an executor-backed asynchronous path. */
  virtual std::future<RuntimeApiResponse> submit_workflow_async(const SubmitWorkflowRequest& request) = 0;
  /** @brief Re-run planning asynchronously for an existing workflow session. */
  virtual std::future<RuntimeApiResponse> reorchestrate_async(const ReorchestrateRequest& request) = 0;
  /** @brief Stream workflow submission progress and final response events. */
  virtual WorkflowEventStream stream_submit_workflow(SubmitWorkflowRequest request) = 0;
  /** @brief Stream re-orchestration progress and final response events. */
  virtual WorkflowEventStream stream_reorchestrate(ReorchestrateRequest request) = 0;
};

/** @brief Abstract HTTP server for the workflow runtime API. */
class HttpWorkflowApiServer {
 public:
  virtual ~HttpWorkflowApiServer() noexcept = default;

  /** @brief Start accepting HTTP requests. */
  virtual void start() = 0;
  /** @brief Stop the HTTP server and release bound resources. */
  virtual void stop() = 0;
  /** @brief Report whether the server is currently accepting requests. */
  virtual bool running() const = 0;
  /** @brief Return the effective HTTP endpoint, including the chosen port. */
  virtual std::string endpoint() const = 0;
};

/** @brief Abstract HTTP client for the workflow runtime API. */
class HttpWorkflowApiClient {
 public:
  virtual ~HttpWorkflowApiClient() noexcept = default;

  /** @brief Submit a workflow over HTTP and return the final response. */
  virtual RuntimeApiResponse submit(const SubmitWorkflowRequest& request) = 0;
  /** @brief Trigger re-orchestration over HTTP and return the final response. */
  virtual RuntimeApiResponse reorchestrate(const ReorchestrateRequest& request) = 0;
};

/** @brief Abstract asynchronous gRPC server for the workflow runtime API. */
class AsyncGrpcWorkflowApiServer {
 public:
  virtual ~AsyncGrpcWorkflowApiServer() noexcept = default;

  /** @brief Start accepting gRPC requests. */
  virtual void start() = 0;
  /** @brief Stop the gRPC server and release bound resources. */
  virtual void stop() = 0;
  /** @brief Report whether the server is currently accepting requests. */
  virtual bool running() const = 0;
  /** @brief Return the effective gRPC endpoint, including the chosen port. */
  virtual std::string endpoint() const = 0;
};

/** @brief Abstract gRPC client for unary and server-streaming runtime API calls. */
class AsyncGrpcWorkflowApiClient {
 public:
  virtual ~AsyncGrpcWorkflowApiClient() noexcept = default;

  /** @brief Submit a workflow over gRPC on the asynchronous client path. */
  virtual std::future<RuntimeApiResponse> submit_async(const SubmitWorkflowRequest& request) = 0;
  /** @brief Trigger re-orchestration over gRPC on the asynchronous client path. */
  virtual std::future<RuntimeApiResponse> reorchestrate_async(const ReorchestrateRequest& request) = 0;
  /** @brief Stream workflow submission progress and final response events over gRPC. */
  virtual WorkflowEventStream submit_stream(SubmitWorkflowRequest request) = 0;
  /** @brief Stream re-orchestration progress and final response events over gRPC. */
  virtual WorkflowEventStream reorchestrate_stream(ReorchestrateRequest request) = 0;
};

}  // namespace task_orchestrator::protocol

#endif  // TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__RUNTIME_API_HPP_
