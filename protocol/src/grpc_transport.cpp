#include "protocol/grpc_transport.hpp"

#include <grpcpp/alarm.h>
#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "detail/grpc_transport_detail.hpp"
#include "task_orchestration_service/task_orchestration_service.grpc.pb.h"
#include "utils/logger.hpp"
#include "utils/task_executor.hpp"

namespace task_orchestrator::protocol {
namespace {

using detail::apply_metadata_auth;
using detail::AsyncClientStreamState;
using detail::effective_cq_thread_count;
using detail::GrpcCredentialsBuildResult;
using detail::kBearerPrefix;
using detail::make_client_credentials;
using detail::make_grpc_target;
using detail::make_ready_future;
using detail::make_server_credentials;
using detail::make_stream_generator;
using detail::make_transport_error_event;
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

struct AsyncGrpcServerImpl;
struct AsyncGrpcClientImpl;
class AsyncServerCallBase;

class AsyncServerCallBase {
 public:
  virtual void proceed(bool ok) = 0;

 protected:
  explicit AsyncServerCallBase(AsyncGrpcServerImpl& impl) : impl_(impl) {}
  virtual ~AsyncServerCallBase() = default;

  void add_reference() noexcept { reference_count_.fetch_add(1, std::memory_order_relaxed); }

  void release_reference() noexcept {
    if (reference_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  bool schedule_notification(grpc::Alarm* alarm, bool* notification_pending, std::mutex* mutex);

  AsyncGrpcServerImpl& impl_;

 private:
  std::atomic<int> reference_count_{1};
};

class AsyncClientCallBase {
 public:
  virtual ~AsyncClientCallBase() noexcept = default;
  virtual void complete(bool ok) = 0;
};

struct AsyncGrpcServerImpl {
  WorkflowRuntimeService& service;
  GrpcEndpointOptions options;
  std::shared_ptr<const TlsCredentialProvider> tls_provider;
  pb::WorkflowRuntimeApi::AsyncService async_service;
  std::unique_ptr<grpc::ServerCompletionQueue> completion_queue;
  std::unique_ptr<grpc::Server> server;
  std::vector<std::jthread> completion_queue_threads;
  TaskExecutor& task_executor = get_shared_task_executor();
  std::atomic<bool> is_running{false};
  int bound_port = 0;
  std::shared_ptr<spdlog::logger> logger = get_logger(LogLayer::Application);

  AsyncGrpcServerImpl(WorkflowRuntimeService& runtime_service,
                      GrpcEndpointOptions endpoint_options,
                      std::shared_ptr<const TlsCredentialProvider> tls_credential_provider)
      : service(runtime_service),
        options(std::move(endpoint_options)),
        tls_provider(std::move(tls_credential_provider)) {}

  ~AsyncGrpcServerImpl() noexcept { stop(); }

  void poll_completion_queue() const;

  [[nodiscard]] std::string endpoint() const {
    return std::string(options.use_tls ? "grpcs://" : "grpc://") +
           make_grpc_target(options.bind_address, bound_port == 0 ? options.port : bound_port);
  }

  void stop() {
    if (!is_running.exchange(false)) {
      return;
    }

    if (server) {
      server->Shutdown();
    }
    if (completion_queue) {
      completion_queue->Shutdown();
    }
    completion_queue_threads.clear();
    server.reset();
    completion_queue.reset();
    bound_port = 0;
  }
};

bool AsyncServerCallBase::schedule_notification(grpc::Alarm* alarm, bool* notification_pending, std::mutex* mutex) {
  std::scoped_lock lock(*mutex);
  if (*notification_pending || impl_.completion_queue == nullptr) {
    return false;
  }
  *notification_pending = true;
  alarm->Set(impl_.completion_queue.get(), std::chrono::system_clock::now(), this);
  return true;
}

void AsyncGrpcServerImpl::poll_completion_queue() const {
  if (completion_queue == nullptr) {
    return;
  }

  void* tag = nullptr;
  bool ok = false;
  while (completion_queue->Next(&tag, &ok)) {
    static_cast<AsyncServerCallBase*>(tag)->proceed(ok);
  }
}

template <typename Request, RuntimeApiResponse (WorkflowRuntimeService::*WorkRpc)(const Request&)>
class AsyncUnaryServerCall final : public AsyncServerCallBase {
 public:
  using RequestRpc = std::function<void(grpc::ServerContext*,
                                        Request*,
                                        grpc::ServerAsyncResponseWriter<RuntimeApiResponse>*,
                                        grpc::CompletionQueue*,
                                        grpc::ServerCompletionQueue*,
                                        void*)>;

  AsyncUnaryServerCall(AsyncGrpcServerImpl& impl, RequestRpc request_rpc)
      : AsyncServerCallBase(impl), responder_(&context_), request_rpc_(std::move(request_rpc)) {
    request_next();
  }

  void proceed(const bool ok) override {
    switch (state_) {
      case State::Requesting:
        if (!ok) {
          release_reference();
          return;
        }
        new AsyncUnaryServerCall(impl_, request_rpc_);
        effective_request_ = request_;
        apply_metadata_auth(context_, impl_.options.use_tls, &effective_request_);
        state_ = State::WaitingForResponse;
        add_reference();
        impl_.task_executor.try_submit([this]() {
          execute_request();
          release_reference();
        });
        return;

      case State::WaitingForResponse:
        if (!ok) {
          state_ = State::Completed;
          release_reference();
          return;
        }
        handle_response_ready();
        return;

      case State::Finishing:
        state_ = State::Completed;
        release_reference();
        return;

      case State::Completed:
        release_reference();
        return;
    }
  }

 private:
  enum class State {
    Requesting,
    WaitingForResponse,
    Finishing,
    Completed,
  };

  void request_next() {
    request_rpc_(&context_, &request_, &responder_, impl_.completion_queue.get(), impl_.completion_queue.get(), this);
  }

  void execute_request() {
    const RuntimeApiResponse response = (impl_.service.*WorkRpc)(effective_request_);
    {
      std::scoped_lock lock(mutex_);
      response_ = response;
    }
    const bool notification_scheduled = schedule_notification(&alarm_, &notification_pending_, &mutex_);
    static_cast<void>(notification_scheduled);
  }

  void handle_response_ready() {
    RuntimeApiResponse response;
    {
      std::scoped_lock lock(mutex_);
      notification_pending_ = false;
      response = response_;
    }
    state_ = State::Finishing;
    responder_.Finish(response, grpc::Status::OK, this);
  }

  grpc::ServerContext context_;
  Request request_;
  Request effective_request_;
  grpc::ServerAsyncResponseWriter<RuntimeApiResponse> responder_;
  grpc::Alarm alarm_;
  RequestRpc request_rpc_;
  std::mutex mutex_;
  RuntimeApiResponse response_;
  bool notification_pending_ = false;
  State state_ = State::Requesting;
};

template <typename Request, WorkflowEventStream (WorkflowRuntimeService::*WorkRpc)(Request)>
class AsyncStreamingServerCall final : public AsyncServerCallBase {
 public:
  using RequestRpc = std::function<void(grpc::ServerContext*,
                                        Request*,
                                        grpc::ServerAsyncWriter<WorkflowEvent>*,
                                        grpc::CompletionQueue*,
                                        grpc::ServerCompletionQueue*,
                                        void*)>;

  AsyncStreamingServerCall(AsyncGrpcServerImpl& impl, RequestRpc request_rpc)
      : AsyncServerCallBase(impl), writer_(&context_), request_rpc_(std::move(request_rpc)) {
    request_next();
  }

  void proceed(const bool ok) override {
    switch (state_) {
      case State::Requesting:
        if (!ok) {
          release_reference();
          return;
        }
        new AsyncStreamingServerCall(impl_, request_rpc_);
        effective_request_ = request_;
        workflow_id_ = workflow_id_for_request(effective_request_);
        apply_metadata_auth(context_, impl_.options.use_tls, &effective_request_);
        state_ = State::WaitingForEvent;
        add_reference();
        impl_.task_executor.try_submit([this]() {
          produce_events();
          release_reference();
        });
        return;

      case State::WaitingForEvent:
        if (!ok) {
          state_ = State::Completed;
          release_reference();
          return;
        }
        handle_notification();
        return;

      case State::Writing:
        handle_write_complete(ok);
        return;

      case State::Finishing:
        state_ = State::Completed;
        release_reference();
        return;

      case State::Completed:
        release_reference();
        return;
    }
  }

 private:
  enum class State {
    Requesting,
    WaitingForEvent,
    Writing,
    Finishing,
    Completed,
  };

  void request_next() {
    request_rpc_(&context_, &request_, &writer_, impl_.completion_queue.get(), impl_.completion_queue.get(), this);
  }

  void produce_events() {
    WorkflowEventStream event_stream = (impl_.service.*WorkRpc)(std::move(effective_request_));
    for (WorkflowEvent event : event_stream) {
      if (!enqueue_event(std::move(event))) {
        break;
      }
    }
    if (event_stream.failed()) {
      const bool error_event_enqueued =
          enqueue_event(make_transport_error_event(workflow_id_, std::string(event_stream.error_message())));
      static_cast<void>(error_event_enqueued);
    }
    mark_producer_complete();
  }

  [[nodiscard]] bool enqueue_event(WorkflowEvent event) {
    std::scoped_lock lock(mutex_);
    if (client_closed_) {
      return false;
    }
    pending_events_.push_back(std::move(event));
    schedule_notification_locked();
    return true;
  }

  void mark_producer_complete() {
    std::scoped_lock lock(mutex_);
    producer_complete_ = true;
    schedule_notification_locked();
  }

  void schedule_notification_locked() {
    if (notification_pending_ || impl_.completion_queue == nullptr) {
      return;
    }
    notification_pending_ = true;
    alarm_.Set(impl_.completion_queue.get(), std::chrono::system_clock::now(), this);
  }

  void handle_notification() {
    bool should_finish = false;
    bool should_write = false;
    {
      std::scoped_lock lock(mutex_);
      notification_pending_ = false;
      if (!pending_events_.empty()) {
        current_event_ = std::move(pending_events_.front());
        pending_events_.pop_front();
        state_ = State::Writing;
        should_write = true;
      } else if (producer_complete_) {
        state_ = State::Finishing;
        should_finish = true;
      }
    }

    if (should_write) {
      writer_.Write(current_event_, this);
      return;
    }
    if (should_finish) {
      writer_.Finish(grpc::Status::OK, this);
    }
  }

  void handle_write_complete(const bool ok) {
    if (!ok) {
      {
        std::scoped_lock lock(mutex_);
        client_closed_ = true;
        pending_events_.clear();
      }
      state_ = State::Completed;
      release_reference();
      return;
    }

    bool should_finish = false;
    bool should_write = false;
    {
      std::scoped_lock lock(mutex_);
      if (!pending_events_.empty()) {
        current_event_ = std::move(pending_events_.front());
        pending_events_.pop_front();
        should_write = true;
      } else if (producer_complete_) {
        state_ = State::Finishing;
        should_finish = true;
      } else {
        state_ = State::WaitingForEvent;
      }
    }

    if (should_write) {
      state_ = State::Writing;
      writer_.Write(current_event_, this);
      return;
    }
    if (should_finish) {
      writer_.Finish(grpc::Status::OK, this);
    }
  }

  grpc::ServerContext context_;
  Request request_;
  Request effective_request_;
  grpc::ServerAsyncWriter<WorkflowEvent> writer_;
  grpc::Alarm alarm_;
  RequestRpc request_rpc_;
  std::mutex mutex_;
  std::deque<WorkflowEvent> pending_events_;
  WorkflowEvent current_event_;
  std::string workflow_id_;
  bool producer_complete_ = false;
  bool client_closed_ = false;
  bool notification_pending_ = false;
  State state_ = State::Requesting;
};

struct AsyncGrpcClientImpl {
  GrpcClientOptions options;
  LoadedTlsClientConfig tls_config;
  std::shared_ptr<grpc::ChannelInterface> channel;
  std::unique_ptr<pb::WorkflowRuntimeApi::Stub> stub;
  grpc::CompletionQueue completion_queue;
  std::jthread completion_queue_thread;
  std::string startup_error_message;
  std::shared_ptr<spdlog::logger> logger = get_logger(LogLayer::Application);

  explicit AsyncGrpcClientImpl(GrpcClientOptions client_options,
                               const std::shared_ptr<const TlsCredentialProvider>& tls_provider)
      : options(std::move(client_options)) {
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
    completion_queue_thread = std::jthread([this]() { poll_completion_queue(); });
  }

  ~AsyncGrpcClientImpl() noexcept { completion_queue.Shutdown(); }

  void poll_completion_queue() {
    void* tag = nullptr;
    bool ok = false;
    while (completion_queue.Next(&tag, &ok)) {
      static_cast<AsyncClientCallBase*>(tag)->complete(ok);
    }
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

  template <typename Request, typename StartRpc>
  std::future<RuntimeApiResponse> invoke_async(Request request, StartRpc&& start_rpc) {
    if (const auto validation_error = validate_grpc_client_invocation_state(startup_error_message, stub.get());
        validation_error.has_value()) {
      return make_ready_future(*validation_error);
    }

    class UnaryClientCall final : public AsyncClientCallBase {
     public:
      UnaryClientCall(AsyncGrpcClientImpl& impl, Request current_request, StartRpc&& current_start_rpc)
          : impl_(impl), request_(std::move(current_request)) {
        impl_.populate_client_context(&context_);
        reader_ = current_start_rpc(*impl_.stub, &context_, request_, &impl_.completion_queue);
      }

      [[nodiscard]] std::future<RuntimeApiResponse> future() { return promise_.get_future(); }

      void start() {
        if (reader_ == nullptr) {
          promise_.set_value(
              make_transport_error_response("gRPC transport failed: asynchronous response reader is not initialized."));
          delete this;
          return;
        }
        reader_->Finish(&response_, &status_, this);
      }

      void complete(const bool ok) override {
        if (!ok) {
          promise_.set_value(
              make_transport_error_response("gRPC transport failed: asynchronous completion queue shutdown."));
          delete this;
          return;
        }

        if (!status_.ok()) {
          impl_.logger->warn(
              "gRPC request to {}:{} failed: {}", impl_.options.host, impl_.options.port, status_.error_message());
          promise_.set_value(
              make_transport_error_response(std::string("gRPC transport failed: ") + status_.error_message()));
          delete this;
          return;
        }

        promise_.set_value(std::move(response_));
        delete this;
      }

     private:
      AsyncGrpcClientImpl& impl_;
      Request request_;
      grpc::ClientContext context_;
      RuntimeApiResponse response_;
      grpc::Status status_;
      std::promise<RuntimeApiResponse> promise_;
      std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<RuntimeApiResponse>> reader_;
    };

    auto* call = new UnaryClientCall(*this, std::move(request), std::forward<StartRpc>(start_rpc));
    std::future<RuntimeApiResponse> future = call->future();
    call->start();
    return future;
  }

  template <typename Request, typename StartRead>
  WorkflowEventStream invoke_stream(Request request, StartRead start_read) {
    if (const auto validation_error = validate_grpc_client_invocation_state(startup_error_message, stub.get());
        validation_error.has_value()) {
      co_yield make_transport_error_event(workflow_id_for_request(request), validation_error->error_message());
      co_return;
    }

    class StreamingClientCall final : public AsyncClientCallBase {
     public:
      StreamingClientCall(AsyncGrpcClientImpl& impl,
                          Request current_request,
                          StartRead&& current_start_read,
                          std::shared_ptr<AsyncClientStreamState> stream_state)
          : impl_(impl),
            request_(std::move(current_request)),
            stream_state_(std::move(stream_state)),
            context_(std::make_shared<grpc::ClientContext>()) {
        impl_.populate_client_context(context_.get());
        stream_state_->set_cancel_callback([context = context_]() { context->TryCancel(); });
        reader_ = current_start_read(*impl_.stub, context_.get(), request_, &impl_.completion_queue);
      }

      void start() {
        if (reader_ == nullptr) {
          stream_state_->push_event(make_transport_error_event(
              workflow_id_for_request(request_), "gRPC transport failed: streaming reader is not initialized."));
          finish_and_delete();
          return;
        }
        state_ = State::Starting;
        reader_->StartCall(this);
      }

      void complete(const bool ok) override {
        switch (state_) {
          case State::Starting:
            if (!ok) {
              if (!stream_state_->cancel_requested()) {
                stream_state_->push_event(make_transport_error_event(
                    workflow_id_for_request(request_), "gRPC transport failed: asynchronous stream start failed."));
              }
              finish_and_delete();
              return;
            }
            state_ = State::Reading;
            reader_->Read(&read_event_, this);
            return;

          case State::Reading:
            if (ok) {
              stream_state_->push_event(std::move(read_event_));
              read_event_.Clear();
              reader_->Read(&read_event_, this);
              return;
            }
            state_ = State::Finishing;
            reader_->Finish(&status_, this);
            return;

          case State::Finishing:
            if (!ok && !stream_state_->cancel_requested()) {
              stream_state_->push_event(make_transport_error_event(
                  workflow_id_for_request(request_), "gRPC transport failed: asynchronous completion queue shutdown."));
            } else if (ok && !status_.ok() && !stream_state_->cancel_requested()) {
              impl_.logger->warn("gRPC streaming request to {}:{} failed: {}",
                                 impl_.options.host,
                                 impl_.options.port,
                                 status_.error_message());
              stream_state_->push_event(make_transport_error_event(
                  workflow_id_for_request(request_), std::string("gRPC transport failed: ") + status_.error_message()));
            }
            finish_and_delete();
            return;

          case State::Completed:
            finish_and_delete();
            return;
        }
      }

     private:
      enum class State {
        Starting,
        Reading,
        Finishing,
        Completed,
      };

      void finish_and_delete() {
        stream_state_->clear_cancel_callback();
        stream_state_->close();
        state_ = State::Completed;
        delete this;
      }

      AsyncGrpcClientImpl& impl_;
      Request request_;
      std::shared_ptr<AsyncClientStreamState> stream_state_;
      std::shared_ptr<grpc::ClientContext> context_;
      WorkflowEvent read_event_;
      grpc::Status status_;
      std::unique_ptr<grpc::ClientAsyncReaderInterface<WorkflowEvent>> reader_;
      State state_ = State::Starting;
    };

    auto stream_state = std::make_shared<AsyncClientStreamState>();
    auto* call = new StreamingClientCall(*this, std::move(request), std::forward<StartRead>(start_read), stream_state);
    call->start();
    for (const WorkflowEvent& event : make_stream_generator(stream_state)) {
      co_yield event;
    }
  }
};

}  // namespace

struct GrpcWorkflowApiServer::Impl {
  AsyncGrpcServerImpl value;

  Impl(WorkflowRuntimeService& runtime_service,
       GrpcEndpointOptions endpoint_options,
       std::shared_ptr<const TlsCredentialProvider> tls_provider)
      : value(runtime_service, std::move(endpoint_options), std::move(tls_provider)) {}
};

struct GrpcWorkflowApiClient::Impl {
  AsyncGrpcClientImpl value;

  Impl(GrpcClientOptions client_options, const std::shared_ptr<const TlsCredentialProvider>& tls_provider)
      : value(std::move(client_options), tls_provider) {}
};

GrpcWorkflowApiServer::GrpcWorkflowApiServer(WorkflowRuntimeService& service,
                                             GrpcEndpointOptions options,
                                             std::shared_ptr<const TlsCredentialProvider> tls_provider)
    : impl_(std::make_unique<Impl>(service, std::move(options), std::move(tls_provider))) {}

GrpcWorkflowApiServer::~GrpcWorkflowApiServer() noexcept { stop(); }

void GrpcWorkflowApiServer::start() {
  AsyncGrpcServerImpl& impl = impl_->value;
  if (impl.is_running.exchange(true)) {
    return;
  }

  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> server_credentials = grpc::InsecureServerCredentials();
  if (impl.options.use_tls) {
    const TlsServerLoadResult tls_load_result = impl.tls_provider->load_server_credentials(impl.options.tls);
    if (!tls_load_result.ok()) {
      impl.is_running.store(false);
      impl.logger->error("Failed to initialize gRPC TLS credentials: {}", tls_load_result.error_message);
      return;
    }
    const GrpcCredentialsBuildResult credential_result = make_server_credentials(tls_load_result.value);
    if (!credential_result.ok()) {
      impl.is_running.store(false);
      impl.logger->error("Failed to initialize gRPC server TLS context: {}", credential_result.error_message);
      return;
    }
    server_credentials = credential_result.server_credentials;
  }

  builder.AddListeningPort(
      make_grpc_target(impl.options.bind_address, impl.options.port), server_credentials, &impl.bound_port);
  builder.SetMaxReceiveMessageSize(impl.options.max_receive_message_bytes);
  builder.SetMaxSendMessageSize(impl.options.max_send_message_bytes);
  impl.completion_queue = builder.AddCompletionQueue();
  builder.RegisterService(&impl.async_service);
  impl.server = builder.BuildAndStart();
  if (!impl.server || impl.completion_queue == nullptr) {
    impl.is_running.store(false);
    impl.server.reset();
    impl.completion_queue.reset();
    impl.logger->error("Failed to start gRPC runtime API server.");
    return;
  }

  new AsyncUnaryServerCall<SubmitWorkflowRequest, &WorkflowRuntimeService::submit_workflow>(
      impl,
      [&impl](grpc::ServerContext* context,
              SubmitWorkflowRequest* request,
              grpc::ServerAsyncResponseWriter<RuntimeApiResponse>* responder,
              grpc::CompletionQueue* completion_queue,
              grpc::ServerCompletionQueue* notification_queue,
              void* tag) {
        impl.async_service.RequestSubmitWorkflow(
            context, request, responder, completion_queue, notification_queue, tag);
      });
  new AsyncUnaryServerCall<ReorchestrateRequest, &WorkflowRuntimeService::reorchestrate>(
      impl,
      [&impl](grpc::ServerContext* context,
              ReorchestrateRequest* request,
              grpc::ServerAsyncResponseWriter<RuntimeApiResponse>* responder,
              grpc::CompletionQueue* completion_queue,
              grpc::ServerCompletionQueue* notification_queue,
              void* tag) {
        impl.async_service.RequestReorchestrate(context, request, responder, completion_queue, notification_queue, tag);
      });
  new AsyncStreamingServerCall<SubmitWorkflowRequest, &WorkflowRuntimeService::stream_submit_workflow>(
      impl,
      [&impl](grpc::ServerContext* context,
              SubmitWorkflowRequest* request,
              grpc::ServerAsyncWriter<WorkflowEvent>* writer,
              grpc::CompletionQueue* completion_queue,
              grpc::ServerCompletionQueue* notification_queue,
              void* tag) {
        impl.async_service.RequestStreamSubmitWorkflow(
            context, request, writer, completion_queue, notification_queue, tag);
      });
  new AsyncStreamingServerCall<ReorchestrateRequest, &WorkflowRuntimeService::stream_reorchestrate>(
      impl,
      [&impl](grpc::ServerContext* context,
              ReorchestrateRequest* request,
              grpc::ServerAsyncWriter<WorkflowEvent>* writer,
              grpc::CompletionQueue* completion_queue,
              grpc::ServerCompletionQueue* notification_queue,
              void* tag) {
        impl.async_service.RequestStreamReorchestrate(
            context, request, writer, completion_queue, notification_queue, tag);
      });

  impl.completion_queue_threads.reserve(effective_cq_thread_count(impl.options.completion_queue_threads));
  for (std::size_t index = 0; index < effective_cq_thread_count(impl.options.completion_queue_threads); ++index) {
    impl.completion_queue_threads.emplace_back([&impl]() { impl.poll_completion_queue(); });
  }

  impl.logger->info("gRPC runtime API listening on {}", impl.endpoint());
}

void GrpcWorkflowApiServer::stop() { impl_->value.stop(); }

bool GrpcWorkflowApiServer::running() const { return impl_->value.is_running.load(); }

std::string GrpcWorkflowApiServer::endpoint() const { return impl_->value.endpoint(); }

GrpcWorkflowApiClient::GrpcWorkflowApiClient(GrpcClientOptions options,
                                             const std::shared_ptr<const TlsCredentialProvider>& tls_provider)
    : impl_(std::make_unique<Impl>(std::move(options), tls_provider)) {}

GrpcWorkflowApiClient::~GrpcWorkflowApiClient() noexcept = default;

std::future<RuntimeApiResponse> GrpcWorkflowApiClient::submit_async(const SubmitWorkflowRequest& request) {
  return impl_->value.invoke_async(request,
                                   [](pb::WorkflowRuntimeApi::StubInterface& stub,
                                      grpc::ClientContext* context,
                                      const SubmitWorkflowRequest& current_request,
                                      grpc::CompletionQueue* completion_queue) {
                                     return stub.AsyncSubmitWorkflow(context, current_request, completion_queue);
                                   });
}

std::future<RuntimeApiResponse> GrpcWorkflowApiClient::reorchestrate_async(const ReorchestrateRequest& request) {
  return impl_->value.invoke_async(request,
                                   [](pb::WorkflowRuntimeApi::StubInterface& stub,
                                      grpc::ClientContext* context,
                                      const ReorchestrateRequest& current_request,
                                      grpc::CompletionQueue* completion_queue) {
                                     return stub.AsyncReorchestrate(context, current_request, completion_queue);
                                   });
}

WorkflowEventStream GrpcWorkflowApiClient::submit_stream(SubmitWorkflowRequest request) {
  return impl_->value.invoke_stream(request,
                                    [](pb::WorkflowRuntimeApi::StubInterface& stub,
                                       grpc::ClientContext* context,
                                       const SubmitWorkflowRequest& current_request,
                                       grpc::CompletionQueue* completion_queue) {
                                      return stub.PrepareAsyncStreamSubmitWorkflow(
                                          context, current_request, completion_queue);
                                    });
}

WorkflowEventStream GrpcWorkflowApiClient::reorchestrate_stream(ReorchestrateRequest request) {
  return impl_->value.invoke_stream(request,
                                    [](pb::WorkflowRuntimeApi::StubInterface& stub,
                                       grpc::ClientContext* context,
                                       const ReorchestrateRequest& current_request,
                                       grpc::CompletionQueue* completion_queue) {
                                      return stub.PrepareAsyncStreamReorchestrate(
                                          context, current_request, completion_queue);
                                    });
}

}  // namespace task_orchestrator::protocol
