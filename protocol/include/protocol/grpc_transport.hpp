#ifndef TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__GRPC_TRANSPORT_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__GRPC_TRANSPORT_HPP_

#include <memory>
#include <string>

#include "protocol/runtime_api.hpp"
#include "protocol/tls_credentials.hpp"
#include "utils/task_executor.hpp"

namespace task_orchestrator::protocol {

/** @brief gRPC server implementation of the runtime API. */
class GrpcWorkflowApiServer final : public AsyncGrpcWorkflowApiServer {
 public:
  /** @brief Construct a gRPC server with optional TLS credential loading. */
  GrpcWorkflowApiServer(
      WorkflowRuntimeService& service,
      GrpcEndpointOptions options = {},
      std::shared_ptr<const TlsCredentialProvider> tls_provider = make_default_tls_credential_provider());
  ~GrpcWorkflowApiServer() noexcept override;

  GrpcWorkflowApiServer(const GrpcWorkflowApiServer&) = delete;
  GrpcWorkflowApiServer& operator=(const GrpcWorkflowApiServer&) = delete;

  /** @brief Start accepting gRPC requests. */
  void start() override;
  /** @brief Stop the gRPC server and release bound resources. */
  void stop() override;
  /** @brief Report whether the server is currently running. */
  bool running() const override;
  /** @brief Return the active gRPC endpoint. */
  std::string endpoint() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/** @brief Executor-backed asynchronous gRPC client implementation. */
class GrpcWorkflowApiClient final : public AsyncGrpcWorkflowApiClient {
 public:
  /** @brief Construct a gRPC client with optional TLS and executor overrides. */
  explicit GrpcWorkflowApiClient(
      GrpcClientOptions options = {},
      const std::shared_ptr<const TlsCredentialProvider>& tls_provider = make_default_tls_credential_provider(),
      task_orchestrator::TaskExecutor* task_executor = nullptr);
  ~GrpcWorkflowApiClient() noexcept override;

  /** @brief Submit a workflow over gRPC and resolve the final response asynchronously. */
  std::future<RuntimeApiResponse> submit_async(const SubmitWorkflowRequest& request) override;
  /** @brief Trigger re-orchestration over gRPC and resolve the final response asynchronously. */
  std::future<RuntimeApiResponse> reorchestrate_async(const ReorchestrateRequest& request) override;
  /** @brief Stream workflow submission progress events over gRPC. */
  WorkflowEventStream submit_stream(SubmitWorkflowRequest request) override;
  /** @brief Stream re-orchestration progress events over gRPC. */
  WorkflowEventStream reorchestrate_stream(ReorchestrateRequest request) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace task_orchestrator::protocol

#endif  // TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__GRPC_TRANSPORT_HPP_
