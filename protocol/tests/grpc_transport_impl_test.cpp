#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// NOLINTNEXTLINE(bugprone-suspicious-include): test-only access to implementation internals via Bazel textual_hdrs.
#include "src/grpc_transport.cpp"

namespace task_orchestrator::protocol {
namespace {
namespace tp = task_orchestrator::protocol;

template <typename Result>
std::future<Result> make_ready_future_for_test(Result value) {
  std::promise<Result> promise;
  promise.set_value(std::move(value));
  return promise.get_future();
}

RuntimeApiResponse make_ok_response(std::string workflow_id) {
  RuntimeApiResponse response;
  response.set_ok(true);
  response.mutable_result()->set_ok(true);
  response.mutable_result()->add_assignments()->set_task_id(std::move(workflow_id));
  return response;
}

class MinimalRuntimeService final : public WorkflowRuntimeService {
 public:
  RuntimeApiResponse submit_workflow(const SubmitWorkflowRequest&) override { return make_ok_response("submit"); }

  RuntimeApiResponse reorchestrate(const ReorchestrateRequest&) override { return make_ok_response("replan"); }

  std::future<RuntimeApiResponse> submit_workflow_async(const SubmitWorkflowRequest& request) override {
    return make_ready_future_for_test(submit_workflow(request));
  }

  std::future<RuntimeApiResponse> reorchestrate_async(const ReorchestrateRequest& request) override {
    return make_ready_future_for_test(reorchestrate(request));
  }

  WorkflowEventStream stream_submit_workflow(SubmitWorkflowRequest) override { co_return; }

  WorkflowEventStream stream_reorchestrate(ReorchestrateRequest) override { co_return; }
};

class FailingClientTlsProvider final : public TlsCredentialProvider {
 public:
  TlsServerLoadResult load_server_credentials(const TlsServerConfig&) const override { return {}; }

  TlsClientLoadResult load_client_credentials(const TlsClientConfig&) const override {
    return TlsClientLoadResult{
        .value = {},
        .error_message = "client tls unavailable",
    };
  }
};

class ExposedAsyncServerCall final : public AsyncServerCallBase {
 public:
  explicit ExposedAsyncServerCall(AsyncGrpcServerImpl& impl) : AsyncServerCallBase(impl) {}

  bool schedule(grpc::Alarm* alarm, bool* notification_pending, std::mutex* mutex) {
    return schedule_notification(alarm, notification_pending, mutex);
  }

  void proceed(bool) override {}
};

TEST(GrpcTransportImplTest, ClientImplCoversMetadataAndNullReaderFallbacks) {
  const auto provider = make_default_tls_credential_provider();
  AsyncGrpcClientImpl impl(
      tp::GrpcClientOptions{
          .host = tp::kDefaultLoopbackAddress,
          .port = tp::kDefaultGrpcPort,
          .use_tls = false,
          .tls = {},
          .deadline_ms = tp::kDefaultRequestTimeoutMs,
          .bearer_token = "token-123",
          .api_key = "api-key-123",
      },
      provider);
  EXPECT_TRUE(impl.startup_error_message.empty());
  ASSERT_NE(nullptr, impl.stub.get());

  SubmitWorkflowRequest submit_request;
  submit_request.mutable_config()->set_id("workflow_demo");

  const auto unary_response =
      impl.invoke_async(
              submit_request,
              [](pb::WorkflowRuntimeApi::StubInterface&,
                 grpc::ClientContext*,
                 const SubmitWorkflowRequest&,
                 grpc::CompletionQueue*)
                  -> std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<RuntimeApiResponse>> { return nullptr; })
          .get();
  EXPECT_FALSE(unary_response.ok());
  EXPECT_NE(unary_response.error_message().find("reader is not initialized"), std::string::npos);

  std::vector<WorkflowEvent> stream_events;
  for (WorkflowEvent event : impl.invoke_stream(
           submit_request,
           [](pb::WorkflowRuntimeApi::StubInterface&,
              grpc::ClientContext*,
              const SubmitWorkflowRequest&,
              grpc::CompletionQueue*) -> std::unique_ptr<grpc::ClientAsyncReaderInterface<WorkflowEvent>> {
             return nullptr;
           })) {
    stream_events.push_back(std::move(event));
  }
  ASSERT_EQ(1U, stream_events.size());
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, stream_events.front().type());
  ASSERT_TRUE(stream_events.front().has_response());
  EXPECT_NE(stream_events.front().response().error_message().find("reader is not initialized"), std::string::npos);

  auto failing_provider = std::make_shared<FailingClientTlsProvider>();
  AsyncGrpcClientImpl failing_tls_impl(
      tp::GrpcClientOptions{
          .host = "localhost",
          .port = tp::kDefaultGrpcPort,
          .use_tls = true,
          .tls = {},
          .deadline_ms = tp::kDefaultRequestTimeoutMs,
          .bearer_token = {},
          .api_key = {},
      },
      failing_provider);
  EXPECT_EQ("client tls unavailable", failing_tls_impl.startup_error_message);
}

TEST(GrpcTransportImplTest, ServerImplCoversMissingQueueAndCancelledCallPaths) {
  MinimalRuntimeService service;
  AsyncGrpcServerImpl impl(service,
                           tp::GrpcEndpointOptions{
                               .bind_address = tp::kDefaultLoopbackAddress,
                               .port = 0,
                               .use_tls = false,
                               .tls = {},
                               .completion_queue_threads = 1,
                               .max_receive_message_bytes = static_cast<int>(tp::kDefaultMaxHttpBodyBytes),
                               .max_send_message_bytes = static_cast<int>(tp::kDefaultMaxHttpBodyBytes),
                           },
                           make_default_tls_credential_provider());

  EXPECT_EQ("grpc://127.0.0.1:0", impl.endpoint());
  impl.poll_completion_queue();

  grpc::Alarm alarm;
  bool notification_pending = false;
  std::mutex mutex;
  ExposedAsyncServerCall helper(impl);
  EXPECT_FALSE(helper.schedule(&alarm, &notification_pending, &mutex));
  EXPECT_FALSE(notification_pending);

  auto unary_rpc = [](grpc::ServerContext*,
                      SubmitWorkflowRequest*,
                      grpc::ServerAsyncResponseWriter<RuntimeApiResponse>*,
                      grpc::CompletionQueue*,
                      grpc::ServerCompletionQueue*,
                      void*) {};
  auto* unary_cancelled =
      new AsyncUnaryServerCall<SubmitWorkflowRequest, &WorkflowRuntimeService::submit_workflow>(impl, unary_rpc);
  unary_cancelled->proceed(false);

  auto* unary_waiting =
      new AsyncUnaryServerCall<SubmitWorkflowRequest, &WorkflowRuntimeService::submit_workflow>(impl, unary_rpc);
  unary_waiting->proceed(true);
  unary_waiting->proceed(false);

  auto streaming_rpc = [](grpc::ServerContext*,
                          SubmitWorkflowRequest*,
                          grpc::ServerAsyncWriter<WorkflowEvent>*,
                          grpc::CompletionQueue*,
                          grpc::ServerCompletionQueue*,
                          void*) {};
  auto* streaming_cancelled =
      new AsyncStreamingServerCall<SubmitWorkflowRequest, &WorkflowRuntimeService::stream_submit_workflow>(
          impl, streaming_rpc);
  streaming_cancelled->proceed(false);

  auto* streaming_waiting =
      new AsyncStreamingServerCall<SubmitWorkflowRequest, &WorkflowRuntimeService::stream_submit_workflow>(
          impl, streaming_rpc);
  streaming_waiting->proceed(true);
  streaming_waiting->proceed(false);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace
}  // namespace task_orchestrator::protocol
