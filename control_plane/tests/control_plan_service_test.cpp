#include "control_plane/service/control_plan_service.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "control_plane/store/sqlite_workflow_store.hpp"

namespace {
namespace tcs = task_orchestrator::control_plane::service;
namespace tcp = task_orchestrator::control_plane;
namespace tp = task_orchestrator::protocol;

struct FakeRuntimeState {
  int submit_call_count = 0;
  int stream_submit_call_count = 0;
  int reorchestrate_call_count = 0;
  int stream_reorchestrate_call_count = 0;
  tp::SubmitWorkflowRequest last_submit_request;
  tp::ReorchestrateRequest last_reorchestrate_request;
};

tp::RuntimeApiResponse make_ok_response(std::string_view task_id = "pick",
                                        std::string_view actor_id = "robot_1",
                                        std::int64_t start_time = 0,
                                        std::int64_t end_time = 5) {
  tp::RuntimeApiResponse response;
  response.set_ok(true);
  response.mutable_result()->set_ok(true);
  auto* assignment = response.mutable_result()->add_assignments();
  assignment->set_task_id(std::string(task_id));
  assignment->set_actor_id(std::string(actor_id));
  assignment->set_start_time(start_time);
  assignment->set_end_time(end_time);
  return response;
}

tp::RuntimeApiResponse make_error_response(std::string_view message = "runtime failed") {
  tp::RuntimeApiResponse response;
  response.set_ok(false);
  response.mutable_result()->set_ok(false);
  response.mutable_result()->set_error_message(std::string(message));
  response.set_error_message(std::string(message));
  return response;
}

tp::WorkflowEvent make_event(tp::pb::WorkflowEventType type, std::string_view workflow_id, std::string_view detail) {
  tp::WorkflowEvent event;
  event.set_type(type);
  event.set_workflow_id(std::string(workflow_id));
  event.set_detail(std::string(detail));
  return event;
}

tp::WorkflowEvent make_response_event(tp::pb::WorkflowEventType type,
                                      std::string_view workflow_id,
                                      const tp::RuntimeApiResponse& response,
                                      std::string_view detail) {
  tp::WorkflowEvent event = make_event(type, workflow_id, detail);
  *event.mutable_response() = response;
  return event;
}

std::future<tp::RuntimeApiResponse> make_ready_future(tp::RuntimeApiResponse response) {
  std::promise<tp::RuntimeApiResponse> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

tp::WorkflowRecord make_stored_workflow(std::string_view workflow_id,
                                        tp::WorkflowLifecycleState state,
                                        std::int64_t updated_at_unix_ms) {
  tp::WorkflowRecord workflow;
  workflow.mutable_summary()->set_workflow_id(std::string(workflow_id));
  workflow.mutable_summary()->set_state(state);
  workflow.mutable_summary()->set_created_at_unix_ms(100);
  workflow.mutable_summary()->set_updated_at_unix_ms(updated_at_unix_ms);
  workflow.mutable_config()->set_id(std::string(workflow_id));
  return workflow;
}

std::filesystem::path make_temp_db(std::string_view suffix) {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("task_orchestrator_control_plane_" + std::string(suffix) + "_" + std::to_string(unique));
  return directory / "control_plane.sqlite3";
}

class FakeRuntimeService final : public tp::WorkflowRuntimeService {
 public:
  FakeRuntimeService(std::shared_ptr<FakeRuntimeState> state,
                     tp::RuntimeApiResponse submit_response = make_ok_response(),
                     tp::RuntimeApiResponse reorchestrate_response = make_ok_response("pick", "robot_2", 1, 6),
                     std::vector<tp::WorkflowEvent> submit_events = {},
                     std::vector<tp::WorkflowEvent> reorchestrate_events = {})
      : state_(std::move(state)),
        submit_response_(std::move(submit_response)),
        reorchestrate_response_(std::move(reorchestrate_response)),
        submit_events_(std::move(submit_events)),
        reorchestrate_events_(std::move(reorchestrate_events)) {}

  tp::RuntimeApiResponse submit_workflow(const tp::SubmitWorkflowRequest& request) override {
    ++state_->submit_call_count;
    state_->last_submit_request = request;
    return submit_response_;
  }

  tp::RuntimeApiResponse reorchestrate(const tp::ReorchestrateRequest& request) override {
    ++state_->reorchestrate_call_count;
    state_->last_reorchestrate_request = request;
    return reorchestrate_response_;
  }

  std::future<tp::RuntimeApiResponse> submit_workflow_async(const tp::SubmitWorkflowRequest& request) override {
    return make_ready_future(submit_workflow(request));
  }

  std::future<tp::RuntimeApiResponse> reorchestrate_async(const tp::ReorchestrateRequest& request) override {
    return make_ready_future(reorchestrate(request));
  }

  tp::WorkflowEventStream stream_submit_workflow(tp::SubmitWorkflowRequest request) override {
    ++state_->stream_submit_call_count;
    state_->last_submit_request = request;
    if (!submit_events_.empty()) {
      for (tp::WorkflowEvent event : submit_events_) {
        if (event.workflow_id().empty()) {
          event.set_workflow_id(request.config().id());
        }
        co_yield event;
      }
      co_return;
    }
    co_yield make_event(
        tp::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, request.config().id(), "Workflow accepted by fake runtime.");
    co_yield make_response_event(tp::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED,
                                 request.config().id(),
                                 submit_response_,
                                 "Workflow finished in fake runtime.");
  }

  tp::WorkflowEventStream stream_reorchestrate(tp::ReorchestrateRequest request) override {
    ++state_->stream_reorchestrate_call_count;
    state_->last_reorchestrate_request = request;
    if (!reorchestrate_events_.empty()) {
      for (tp::WorkflowEvent event : reorchestrate_events_) {
        if (event.workflow_id().empty()) {
          event.set_workflow_id(request.workflow_id());
        }
        co_yield event;
      }
      co_return;
    }
    co_yield make_event(
        tp::pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED, request.workflow_id(), "Fake re-orchestration started.");
    co_yield make_response_event(tp::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED,
                                 request.workflow_id(),
                                 reorchestrate_response_,
                                 "Fake re-orchestration finished.");
  }

 private:
  std::shared_ptr<FakeRuntimeState> state_;
  tp::RuntimeApiResponse submit_response_;
  tp::RuntimeApiResponse reorchestrate_response_;
  std::vector<tp::WorkflowEvent> submit_events_;
  std::vector<tp::WorkflowEvent> reorchestrate_events_;
};

TEST(ControlPlanServiceTest, PersistsSubmitHistoryAndServesIdempotentReplay) {
  const std::filesystem::path database_path = make_temp_db("submit");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-submit");
  auto runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService service(std::make_unique<FakeRuntimeService>(runtime_state), store);

  tp::SubmitWorkflowRequest request;
  request.mutable_config()->set_id("wf-1");
  request.set_idempotency_key("idem-1");

  const tp::RuntimeApiResponse first_response = service.submit_workflow(request);
  ASSERT_TRUE(first_response.ok());
  ASSERT_TRUE(first_response.result().ok());
  EXPECT_EQ(1, runtime_state->stream_submit_call_count);

  const tp::RuntimeApiResponse second_response = service.submit_workflow(request);
  ASSERT_TRUE(second_response.ok());
  ASSERT_TRUE(second_response.result().ok());
  EXPECT_EQ(1, runtime_state->stream_submit_call_count);

  const auto workflow = store->get_workflow("wf-1");
  ASSERT_TRUE(workflow.has_value());
  EXPECT_EQ(tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, workflow->summary().state());
  EXPECT_EQ(2, workflow->summary().total_event_count());
  EXPECT_EQ(1, workflow->summary().latest_plan_version());

  const auto events = store->list_events("wf-1");
  ASSERT_EQ(2U, events.size());
  EXPECT_EQ(tp::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, events.front().event().type());
  EXPECT_EQ(tp::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, events.back().event().type());

  const auto idempotency = store->get_idempotency_record("idem-1");
  ASSERT_TRUE(idempotency.has_value());
  EXPECT_EQ("wf-1", idempotency->workflow_id());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(ControlPlanServiceTest, RecoveryRehydratesRuntimeAndRecordsAuditHistory) {
  const std::filesystem::path database_path = make_temp_db("recovery");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-recovery");
  tp::WorkflowRecord workflow;
  workflow.mutable_summary()->set_workflow_id("wf-2");
  workflow.mutable_summary()->set_state(tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED);
  workflow.mutable_summary()->set_created_at_unix_ms(100);
  workflow.mutable_summary()->set_updated_at_unix_ms(100);
  workflow.mutable_config()->set_id("wf-2");
  store->upsert_workflow(workflow);

  auto runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService service(std::make_unique<FakeRuntimeService>(runtime_state), store);

  tp::pb::ClientAuthContext auth_context;
  auth_context.set_secure_transport(true);
  service.recover_active_workflows(auth_context);

  EXPECT_EQ(1, runtime_state->submit_call_count);
  EXPECT_EQ("wf-2", runtime_state->last_submit_request.config().id());
  ASSERT_TRUE(runtime_state->last_submit_request.has_replace_existing());
  EXPECT_TRUE(runtime_state->last_submit_request.replace_existing());
  EXPECT_TRUE(runtime_state->last_submit_request.auth().secure_transport());

  const auto recovered_workflow = store->get_workflow("wf-2");
  ASSERT_TRUE(recovered_workflow.has_value());
  EXPECT_EQ(1, recovered_workflow->summary().total_audit_entry_count());
  EXPECT_GE(recovered_workflow->summary().updated_at_unix_ms(), 100);

  const auto audits = store->list_audit_entries("wf-2");
  ASSERT_EQ(1U, audits.size());
  EXPECT_EQ("recover", audits.front().action());
  EXPECT_EQ("control_plane/recovery", audits.front().actor());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(ControlPlanServiceTest, LifecycleActionsHistoryAndPlanDiffUseStoredState) {
  const std::filesystem::path database_path = make_temp_db("lifecycle");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-lifecycle");
  auto runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService service(
      std::make_unique<FakeRuntimeService>(
          runtime_state, make_ok_response("pick", "robot_1", 0, 5), make_ok_response("pick", "robot_2", 1, 6)),
      store);

  tp::SubmitWorkflowRequest submit_request;
  submit_request.mutable_config()->set_id("wf-3");
  ASSERT_TRUE(service.submit_workflow(submit_request).ok());

  tp::ReorchestrateRequest reorchestrate_request;
  reorchestrate_request.set_workflow_id("wf-3");
  reorchestrate_request.set_expected_plan_version(1);
  ASSERT_TRUE(service.reorchestrate(reorchestrate_request).ok());

  tp::GetPlanDiffRequest diff_request;
  diff_request.set_workflow_id("wf-3");
  diff_request.set_from_plan_version(1);
  diff_request.set_to_plan_version(2);
  const auto diff_response = service.get_plan_diff(diff_request);
  ASSERT_TRUE(diff_response.ok());
  ASSERT_EQ(1, diff_response.diff().changed_assignments_size());
  EXPECT_EQ("robot_1", diff_response.diff().changed_assignments(0).before().actor_id());
  EXPECT_EQ("robot_2", diff_response.diff().changed_assignments(0).after().actor_id());

  tp::GetWorkflowHistoryRequest history_request;
  history_request.set_workflow_id("wf-3");
  history_request.set_max_events(10);
  history_request.set_max_plan_versions(10);
  history_request.set_max_audit_entries(10);
  const auto history = service.get_workflow_history(history_request);
  ASSERT_TRUE(history.ok());
  EXPECT_EQ(4, history.events_size());
  EXPECT_EQ(2, history.plan_versions_size());
  EXPECT_EQ(0, history.audit_entries_size());

  tp::PauseWorkflowRequest pause_request;
  pause_request.set_workflow_id("wf-3");
  pause_request.set_actor("dispatcher");
  const auto pause_response = service.pause_workflow(pause_request);
  ASSERT_TRUE(pause_response.ok());
  EXPECT_EQ(tp::pb::WORKFLOW_LIFECYCLE_STATE_PAUSED, pause_response.workflow().summary().state());

  tp::ResumeWorkflowRequest resume_request;
  resume_request.set_workflow_id("wf-3");
  const auto resume_response = service.resume_workflow(resume_request);
  ASSERT_TRUE(resume_response.ok());
  EXPECT_EQ(tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, resume_response.workflow().summary().state());

  tp::CancelWorkflowRequest cancel_request;
  cancel_request.set_workflow_id("wf-3");
  const auto cancel_response = service.cancel_workflow(cancel_request);
  ASSERT_TRUE(cancel_response.ok());
  EXPECT_EQ(tp::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED, cancel_response.workflow().summary().state());

  tp::GetWorkflowRequest get_request;
  get_request.set_workflow_id("wf-3");
  const auto workflow_response = service.get_workflow(get_request);
  ASSERT_TRUE(workflow_response.ok());
  EXPECT_EQ(tp::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED, workflow_response.workflow().summary().state());
  EXPECT_EQ(3, workflow_response.workflow().summary().total_audit_entry_count());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(ControlPlanServiceTest, RejectsPausedAndConflictingIdempotentRequests) {
  const std::filesystem::path database_path = make_temp_db("rejects");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-rejects");
  auto runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService service(std::make_unique<FakeRuntimeService>(runtime_state), store);

  tp::SubmitWorkflowRequest first_submit;
  first_submit.mutable_config()->set_id("wf-4");
  first_submit.set_idempotency_key("idem-4");
  ASSERT_TRUE(service.submit_workflow(first_submit).ok());

  tp::SubmitWorkflowRequest second_submit = first_submit;
  second_submit.mutable_config()->set_id("wf-4b");
  const auto idempotency_conflict = service.submit_workflow(second_submit);
  EXPECT_FALSE(idempotency_conflict.ok());
  EXPECT_NE(std::string::npos, idempotency_conflict.error_message().find("Idempotency key"));

  tp::PauseWorkflowRequest pause_request;
  pause_request.set_workflow_id("wf-4");
  ASSERT_TRUE(service.pause_workflow(pause_request).ok());

  tp::ReorchestrateRequest paused_request;
  paused_request.set_workflow_id("wf-4");
  const auto paused_response = service.reorchestrate(paused_request);
  EXPECT_FALSE(paused_response.ok());
  EXPECT_NE(std::string::npos, paused_response.error_message().find("paused"));

  tp::ResumeWorkflowRequest resume_request;
  resume_request.set_workflow_id("wf-4");
  ASSERT_TRUE(service.resume_workflow(resume_request).ok());

  tp::ManualInterventionRequest intervention_request;
  intervention_request.set_workflow_id("wf-4");
  intervention_request.set_expected_plan_version(99);
  intervention_request.set_trigger_reorchestration(true);
  const auto intervention_response = service.apply_manual_intervention(intervention_request);
  EXPECT_FALSE(intervention_response.ok());
  EXPECT_NE(std::string::npos, intervention_response.error_message().find("Expected plan version"));

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(ControlPlanServiceTest, SupportsAsyncManualInterventionAndNotFoundBranches) {
  const std::filesystem::path database_path = make_temp_db("async_manual");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-async-manual");
  auto runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService service(std::make_unique<FakeRuntimeService>(runtime_state), store);

  tp::SubmitWorkflowRequest submit_request;
  submit_request.mutable_config()->set_id("wf-5");
  ASSERT_TRUE(service.submit_workflow_async(submit_request).get().ok());

  tp::ReorchestrateRequest reorchestrate_request;
  reorchestrate_request.set_workflow_id("wf-5");
  ASSERT_TRUE(service.reorchestrate_async(reorchestrate_request).get().ok());
  EXPECT_EQ(1, runtime_state->stream_submit_call_count);
  EXPECT_EQ(1, runtime_state->stream_reorchestrate_call_count);

  tp::ListWorkflowsRequest list_request;
  list_request.set_page_size(5);
  const auto list_response = service.list_workflows(list_request);
  ASSERT_EQ(1, list_response.workflows_size());
  EXPECT_EQ("wf-5", list_response.workflows(0).workflow_id());

  tp::SearchWorkflowsRequest search_request;
  search_request.set_query("wf-5");
  const auto search_response = service.search_workflows(search_request);
  ASSERT_EQ(1, search_response.workflows_size());
  EXPECT_EQ("wf-5", search_response.workflows(0).workflow_id());

  tp::GetWorkflowRequest missing_workflow_request;
  missing_workflow_request.set_workflow_id("missing");
  const auto missing_workflow = service.get_workflow(missing_workflow_request);
  EXPECT_FALSE(missing_workflow.ok());
  EXPECT_NE(std::string::npos, missing_workflow.error_message().find("not found"));

  tp::GetWorkflowHistoryRequest missing_history_request;
  missing_history_request.set_workflow_id("missing");
  const auto missing_history = service.get_workflow_history(missing_history_request);
  EXPECT_FALSE(missing_history.ok());
  EXPECT_NE(std::string::npos, missing_history.error_message().find("not found"));

  tp::GetPlanDiffRequest missing_diff_request;
  missing_diff_request.set_workflow_id("wf-5");
  missing_diff_request.set_from_plan_version(99);
  missing_diff_request.set_to_plan_version(100);
  const auto missing_diff = service.get_plan_diff(missing_diff_request);
  EXPECT_FALSE(missing_diff.ok());
  EXPECT_NE(std::string::npos, missing_diff.error_message().find("not found"));

  tp::PauseWorkflowRequest missing_pause_request;
  missing_pause_request.set_workflow_id("missing");
  EXPECT_FALSE(service.pause_workflow(missing_pause_request).ok());

  tp::ResumeWorkflowRequest missing_resume_request;
  missing_resume_request.set_workflow_id("missing");
  EXPECT_FALSE(service.resume_workflow(missing_resume_request).ok());

  tp::CancelWorkflowRequest missing_cancel_request;
  missing_cancel_request.set_workflow_id("missing");
  EXPECT_FALSE(service.cancel_workflow(missing_cancel_request).ok());

  tp::ManualInterventionRequest missing_intervention_request;
  missing_intervention_request.set_workflow_id("missing");
  EXPECT_FALSE(service.apply_manual_intervention(missing_intervention_request).ok());

  tp::ManualInterventionRequest intervention_request;
  intervention_request.set_workflow_id("wf-5");
  intervention_request.set_idempotency_key("manual-idem");
  intervention_request.set_trigger_reorchestration(true);
  const auto intervention_response = service.apply_manual_intervention(intervention_request);
  ASSERT_TRUE(intervention_response.ok());
  ASSERT_TRUE(runtime_state->last_reorchestrate_request.has_idempotency_key());
  EXPECT_EQ("manual-idem", runtime_state->last_reorchestrate_request.idempotency_key());
  EXPECT_EQ("wf-5", intervention_response.workflow().summary().workflow_id());
  EXPECT_EQ(1, intervention_response.workflow().summary().total_audit_entry_count());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(ControlPlanServiceTest, RecoverySkipsTerminalStatesAndPersistsFailedTerminalEvents) {
  const std::filesystem::path database_path = make_temp_db("recovery_skip");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-recovery-skip");
  store->upsert_workflow(make_stored_workflow("wf-active", tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, 100));
  store->upsert_workflow(make_stored_workflow("wf-cancelled", tp::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED, 101));
  store->upsert_workflow(make_stored_workflow("wf-failed", tp::pb::WORKFLOW_LIFECYCLE_STATE_FAILED, 102));
  store->upsert_workflow(make_stored_workflow("wf-unspecified", tp::pb::WORKFLOW_LIFECYCLE_STATE_UNSPECIFIED, 103));

  auto runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService recovery_service(
      std::make_unique<FakeRuntimeService>(runtime_state, make_error_response("rehydration failed")), store);

  tp::pb::ClientAuthContext auth_context;
  auth_context.set_secure_transport(true);
  recovery_service.recover_active_workflows(auth_context);
  EXPECT_EQ(1, runtime_state->submit_call_count);
  EXPECT_EQ("wf-active", runtime_state->last_submit_request.config().id());
  EXPECT_TRUE(store->list_audit_entries("wf-cancelled").empty());
  EXPECT_TRUE(store->list_audit_entries("wf-failed").empty());
  EXPECT_TRUE(store->list_audit_entries("wf-unspecified").empty());

  std::vector<tp::WorkflowEvent> submit_events;
  submit_events.push_back(make_event(tp::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, "wf-events", "accepted"));
  submit_events.push_back(make_event(tp::pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED, "wf-events", "planned"));
  submit_events.push_back(
      make_event(tp::pb::WORKFLOW_EVENT_TYPE_RUNTIME_OVERRIDE_APPLIED, "wf-events", "override applied"));
  submit_events.push_back(make_event(tp::pb::WORKFLOW_EVENT_TYPE_TASK_COMPLETED, "wf-events", "task completed"));
  submit_events.push_back(make_event(tp::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, "wf-events", "request rejected"));
  submit_events.push_back(make_event(tp::pb::WORKFLOW_EVENT_TYPE_UNSPECIFIED, "wf-events", "unspecified"));
  submit_events.push_back(make_response_event(
      tp::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, "wf-events", make_error_response("terminal failure"), "finished"));

  auto event_runtime_state = std::make_shared<FakeRuntimeState>();
  tcs::ControlPlanService event_service(
      std::make_unique<FakeRuntimeService>(
          event_runtime_state, make_ok_response(), make_ok_response("pick", "robot_2", 1, 6), std::move(submit_events)),
      store);

  tp::SubmitWorkflowRequest submit_request;
  submit_request.mutable_config()->set_id("wf-events");
  const auto failed_response = event_service.submit_workflow(submit_request);
  EXPECT_FALSE(failed_response.ok());
  EXPECT_EQ("terminal failure", failed_response.error_message());

  const auto stored_workflow = store->get_workflow("wf-events");
  ASSERT_TRUE(stored_workflow.has_value());
  EXPECT_EQ(tp::pb::WORKFLOW_LIFECYCLE_STATE_FAILED, stored_workflow->summary().state());
  EXPECT_EQ("terminal failure", stored_workflow->summary().last_error_message());
  EXPECT_EQ(7, stored_workflow->summary().total_event_count());

  std::filesystem::remove_all(database_path.parent_path());
}

}  // namespace
