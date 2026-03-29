#include "protocol/grpc_transport.hpp"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>

#include "detail/grpc_transport_detail.hpp"
#include "task_orchestration_service/task_orchestration_service.grpc.pb.h"
#include "utils/logger.hpp"
#include "utils/task_executor.hpp"

namespace task_orchestrator::protocol {
namespace {

using detail::apply_metadata_auth;
using detail::GrpcCredentialsBuildResult;
using detail::kBearerPrefix;
using detail::make_client_credentials;
using detail::make_grpc_target;
using detail::make_ready_future;
using detail::make_server_credentials;
using detail::make_transport_error_response;
using detail::resolve_expected_peer_name;
using detail::validate_grpc_client_invocation_state;

template <typename Request>
std::string workflow_id_for_request(const Request& request);

template <>
std::string workflow_id_for_request<SubmitWorkflowRequest>(const SubmitWorkflowRequest& request) {
  return request.config().id();
}

template <>
std::string workflow_id_for_request<ReorchestrateRequest>(const ReorchestrateRequest& request) {
  return request.workflow_id();
}

WorkflowEvent make_transport_error_event(std::string workflow_id, std::string detail) {
  WorkflowEvent event;
  event.set_type(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED);
  event.set_workflow_id(std::move(workflow_id));
  event.set_detail(detail);
  *event.mutable_response() = make_transport_error_response(event.detail());
  return event;
}

class RuntimeApiGrpcService final : public pb::WorkflowRuntimeApi::Service {
 public:
  RuntimeApiGrpcService(WorkflowRuntimeService& runtime_service, GrpcEndpointOptions endpoint_options)
      : service_(runtime_service), options_(std::move(endpoint_options)) {}

  grpc::Status SubmitWorkflow(grpc::ServerContext* context,
                              const SubmitWorkflowRequest* request,
                              RuntimeApiResponse* response) override {
    SubmitWorkflowRequest effective_request(*request);
    apply_metadata_auth(*context, options_.use_tls, &effective_request);
    *response = service_.submit_workflow(effective_request);
    return grpc::Status::OK;
  }

  grpc::Status Reorchestrate(grpc::ServerContext* context,
                             const ReorchestrateRequest* request,
                             RuntimeApiResponse* response) override {
    ReorchestrateRequest effective_request(*request);
    apply_metadata_auth(*context, options_.use_tls, &effective_request);
    *response = service_.reorchestrate(effective_request);
    return grpc::Status::OK;
  }

  grpc::Status StreamSubmitWorkflow(grpc::ServerContext* context,
                                    const SubmitWorkflowRequest* request,
                                    grpc::ServerWriter<WorkflowEvent>* writer) override {
    SubmitWorkflowRequest effective_request(*request);
    apply_metadata_auth(*context, options_.use_tls, &effective_request);
    for (const WorkflowEvent& event : service_.stream_submit_workflow(std::move(effective_request))) {
      if (!writer->Write(event)) {
        break;
      }
    }
    return grpc::Status::OK;
  }

  grpc::Status StreamReorchestrate(grpc::ServerContext* context,
                                   const ReorchestrateRequest* request,
                                   grpc::ServerWriter<WorkflowEvent>* writer) override {
    ReorchestrateRequest effective_request(*request);
    apply_metadata_auth(*context, options_.use_tls, &effective_request);
    for (const WorkflowEvent& event : service_.stream_reorchestrate(std::move(effective_request))) {
      if (!writer->Write(event)) {
        break;
      }
    }
    return grpc::Status::OK;
  }

 private:
  WorkflowRuntimeService& service_;
  GrpcEndpointOptions options_;
};

}  // namespace

struct GrpcWorkflowApiServer::Impl {
  WorkflowRuntimeService& service;
  GrpcEndpointOptions options;
  std::shared_ptr<const TlsCredentialProvider> tls_provider;
  RuntimeApiGrpcService grpc_service;
  std::unique_ptr<grpc::Server> server;
  std::atomic<bool> is_running{false};
  int bound_port = 0;
  std::shared_ptr<spdlog::logger> logger = get_logger(LogLayer::Application);

  Impl(WorkflowRuntimeService& runtime_service,
       GrpcEndpointOptions endpoint_options,
       std::shared_ptr<const TlsCredentialProvider> tls_credential_provider)
      : service(runtime_service),
        options(std::move(endpoint_options)),
        tls_provider(std::move(tls_credential_provider)),
        grpc_service(service, options) {}

  void start() {
    if (is_running.exchange(true)) {
      return;
    }

    grpc::ServerBuilder builder;
    std::shared_ptr<grpc::ServerCredentials> server_credentials = grpc::InsecureServerCredentials();
    if (options.use_tls) {
      const TlsServerLoadResult tls_load_result = tls_provider->load_server_credentials(options.tls);
      if (!tls_load_result.ok()) {
        is_running.store(false);
        logger->error("Failed to initialize gRPC TLS credentials: {}", tls_load_result.error_message);
        return;
      }
      const GrpcCredentialsBuildResult credential_result = make_server_credentials(tls_load_result.value);
      if (!credential_result.ok()) {
        is_running.store(false);
        logger->error("Failed to initialize gRPC server TLS context: {}", credential_result.error_message);
        return;
      }
      server_credentials = credential_result.server_credentials;
    }
    builder.AddListeningPort(make_grpc_target(options.bind_address, options.port), server_credentials, &bound_port);
    builder.SetMaxReceiveMessageSize(options.max_receive_message_bytes);
    builder.SetMaxSendMessageSize(options.max_send_message_bytes);
    builder.RegisterService(&grpc_service);
    server = builder.BuildAndStart();
    if (!server) {
      is_running.store(false);
      logger->error("Failed to start gRPC runtime API server.");
      return;
    }
    logger->info("gRPC runtime API listening on {}", endpoint());
  }

  void stop() {
    if (!is_running.exchange(false)) {
      return;
    }
    if (server) {
      server->Shutdown();
      server.reset();
    }
    bound_port = 0;
  }

  [[nodiscard]] std::string endpoint() const {
    return std::string(options.use_tls ? "grpcs://" : "grpc://") +
           make_grpc_target(options.bind_address, bound_port == 0 ? options.port : bound_port);
  }
};

struct GrpcWorkflowApiClient::Impl {
  GrpcClientOptions options;
  LoadedTlsClientConfig tls_config;
  TaskExecutor& task_executor;
  std::shared_ptr<grpc::ChannelInterface> channel;
  std::unique_ptr<pb::WorkflowRuntimeApi::Stub> stub;
  std::string startup_error_message;
  std::shared_ptr<spdlog::logger> logger = get_logger(LogLayer::Application);

  Impl(GrpcClientOptions client_options,
       const std::shared_ptr<const TlsCredentialProvider>& tls_provider,
       TaskExecutor* configured_task_executor)
      : options(std::move(client_options)),
        task_executor(configured_task_executor != nullptr ? *configured_task_executor : get_shared_task_executor()) {
    grpc::ChannelArguments channel_arguments;
    channel_arguments.SetMaxReceiveMessageSize(-1);
    channel_arguments.SetMaxSendMessageSize(-1);

    const std::shared_ptr<grpc::ChannelCredentials> channel_credentials =
        [&]() -> std::shared_ptr<grpc::ChannelCredentials> {
      if (!options.use_tls) {
        return grpc::InsecureChannelCredentials();
      }

      const TlsClientLoadResult tls_load_result = tls_provider->load_client_credentials(options.tls);
      if (!tls_load_result.ok()) {
        startup_error_message = tls_load_result.error_message;
        return {};
      }
      tls_config = tls_load_result.value;
      const std::string expected_peer_name = resolve_expected_peer_name(options, tls_config);
      if (!expected_peer_name.empty()) {
        channel_arguments.SetSslTargetNameOverride(expected_peer_name);
      }
      const GrpcCredentialsBuildResult credential_result = make_client_credentials(tls_config);
      if (!credential_result.ok()) {
        startup_error_message = credential_result.error_message;
        return {};
      }
      return credential_result.channel_credentials;
    }();

    if (!startup_error_message.empty()) {
      logger->error("Failed to initialize gRPC client transport: {}", startup_error_message);
      return;
    }

    channel =
        grpc::CreateCustomChannel(make_grpc_target(options.host, options.port), channel_credentials, channel_arguments);
    stub = pb::WorkflowRuntimeApi::NewStub(channel);
  }

  void populate_client_context(grpc::ClientContext* const context) const {
    context->set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(options.deadline_ms));
    if (!options.bearer_token.empty()) {
      context->AddMetadata(std::string(kAuthorizationHeader), std::string(kBearerPrefix) + options.bearer_token);
    }
    if (!options.api_key.empty()) {
      context->AddMetadata(std::string(kApiKeyHeader), options.api_key);
    }
  }

  template <typename Request, typename Invoke>
  std::future<RuntimeApiResponse> invoke_async(Request request, Invoke&& invoke) {
    if (const auto validation_error = validate_grpc_client_invocation_state(startup_error_message, stub.get());
        validation_error.has_value()) {
      return make_ready_future(*validation_error);
    }

    return task_executor.try_submit(
        [this, request = std::move(request), invoke = std::forward<Invoke>(invoke)]() mutable {
          grpc::ClientContext context;
          populate_client_context(&context);

          RuntimeApiResponse response;
          if (grpc::Status status = invoke(context, request, response); !status.ok()) {
            logger->warn("gRPC request to {}:{} failed: {}", options.host, options.port, status.error_message());
            return make_transport_error_response(std::string("gRPC transport failed: ") + status.error_message());
          }
          return response;
        });
  }

  template <typename Request, typename StartRead>
  WorkflowEventStream invoke_stream(Request request, StartRead start_read) {
    if (const auto validation_error = validate_grpc_client_invocation_state(startup_error_message, stub.get());
        validation_error.has_value()) {
      co_yield make_transport_error_event(workflow_id_for_request(request), validation_error->error_message());
      co_return;
    }

    grpc::ClientContext context;
    populate_client_context(&context);
    auto reader = start_read(context, request);
    if (reader == nullptr) {
      co_yield make_transport_error_event(workflow_id_for_request(request),
                                          "gRPC transport failed: streaming reader is not initialized.");
      co_return;
    }

    WorkflowEvent event;
    while (reader->Read(&event)) {
      co_yield event;
    }

    const grpc::Status status = reader->Finish();
    if (!status.ok()) {
      logger->warn("gRPC streaming request to {}:{} failed: {}", options.host, options.port, status.error_message());
      co_yield make_transport_error_event(workflow_id_for_request(request),
                                          std::string("gRPC transport failed: ") + status.error_message());
    }
  }
};

GrpcWorkflowApiServer::GrpcWorkflowApiServer(WorkflowRuntimeService& service,
                                             GrpcEndpointOptions options,
                                             std::shared_ptr<const TlsCredentialProvider> tls_provider)
    : impl_(std::make_unique<Impl>(service, std::move(options), std::move(tls_provider))) {}

GrpcWorkflowApiServer::~GrpcWorkflowApiServer() noexcept { stop(); }

void GrpcWorkflowApiServer::start() { impl_->start(); }

void GrpcWorkflowApiServer::stop() { impl_->stop(); }

bool GrpcWorkflowApiServer::running() const { return impl_->is_running.load(); }

std::string GrpcWorkflowApiServer::endpoint() const { return impl_->endpoint(); }

GrpcWorkflowApiClient::GrpcWorkflowApiClient(GrpcClientOptions options,
                                             const std::shared_ptr<const TlsCredentialProvider>& tls_provider,
                                             TaskExecutor* task_executor)
    : impl_(std::make_unique<Impl>(std::move(options), tls_provider, task_executor)) {}

GrpcWorkflowApiClient::~GrpcWorkflowApiClient() noexcept = default;

std::future<RuntimeApiResponse> GrpcWorkflowApiClient::submit_async(const SubmitWorkflowRequest& request) {
  return impl_->invoke_async(
      request,
      [this](grpc::ClientContext& context, const SubmitWorkflowRequest& current_request, RuntimeApiResponse& response) {
        return impl_->stub->SubmitWorkflow(&context, current_request, &response);
      });
}

std::future<RuntimeApiResponse> GrpcWorkflowApiClient::reorchestrate_async(const ReorchestrateRequest& request) {
  return impl_->invoke_async(
      request,
      [this](grpc::ClientContext& context, const ReorchestrateRequest& current_request, RuntimeApiResponse& response) {
        return impl_->stub->Reorchestrate(&context, current_request, &response);
      });
}

WorkflowEventStream GrpcWorkflowApiClient::submit_stream(SubmitWorkflowRequest request) {
  return impl_->invoke_stream(std::move(request),
                              [this](grpc::ClientContext& context, const SubmitWorkflowRequest& current_request) {
                                return impl_->stub->StreamSubmitWorkflow(&context, current_request);
                              });
}

WorkflowEventStream GrpcWorkflowApiClient::reorchestrate_stream(ReorchestrateRequest request) {
  return impl_->invoke_stream(std::move(request),
                              [this](grpc::ClientContext& context, const ReorchestrateRequest& current_request) {
                                return impl_->stub->StreamReorchestrate(&context, current_request);
                              });
}

}  // namespace task_orchestrator::protocol
