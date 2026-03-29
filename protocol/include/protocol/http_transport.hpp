#ifndef TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__HTTP_TRANSPORT_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__HTTP_TRANSPORT_HPP_

#include <memory>
#include <string>

#include "protocol/operator_api.hpp"
#include "protocol/runtime_api.hpp"
#include "protocol/tls_credentials.hpp"

namespace task_orchestrator::protocol {

/** @brief Boost.Beast-based HTTP server implementation of the runtime API. */
class BeastHttpWorkflowApiServer final : public HttpWorkflowApiServer {
 public:
  /** @brief Construct an HTTP server with optional TLS credential loading. */
  BeastHttpWorkflowApiServer(
      WorkflowRuntimeService& service,
      HttpEndpointOptions options = {},
      std::shared_ptr<const TlsCredentialProvider> tls_provider = make_default_tls_credential_provider(),
      WorkflowOperatorService* operator_service = nullptr,
      WorkflowOperatorEventService* operator_event_service = nullptr);
  ~BeastHttpWorkflowApiServer() noexcept override;

  BeastHttpWorkflowApiServer(const BeastHttpWorkflowApiServer&) = delete;
  BeastHttpWorkflowApiServer& operator=(const BeastHttpWorkflowApiServer&) = delete;

  /** @brief Start accepting HTTP requests. */
  void start() override;
  /** @brief Stop the HTTP server and release bound resources. */
  void stop() override;
  /** @brief Report whether the server is currently running. */
  bool running() const override;
  /** @brief Return the active HTTP endpoint. */
  std::string endpoint() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/** @brief Boost.Beast-based HTTP client implementation of the runtime API. */
class BeastHttpWorkflowApiClient final : public HttpWorkflowApiClient {
 public:
  /** @brief Construct an HTTP client with optional TLS credential loading. */
  explicit BeastHttpWorkflowApiClient(
      HttpClientOptions options = {},
      std::shared_ptr<const TlsCredentialProvider> tls_provider = make_default_tls_credential_provider());
  ~BeastHttpWorkflowApiClient() noexcept override;

  /** @brief Submit a workflow over HTTP and return the final response. */
  RuntimeApiResponse submit(const SubmitWorkflowRequest& request) override;
  /** @brief Trigger re-orchestration over HTTP and return the final response. */
  RuntimeApiResponse reorchestrate(const ReorchestrateRequest& request) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace task_orchestrator::protocol

#endif  // TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__HTTP_TRANSPORT_HPP_
