#include "operator/operator_service.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "control_plane/integration/connector_registry.hpp"
#include "control_plane/store/sqlite_workflow_store.hpp"

namespace {
namespace toa = task_orchestrator::app::operator_api;
namespace tcp = task_orchestrator::control_plane;
namespace tp = task_orchestrator::protocol;

std::int64_t unix_time_ms_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::future<tp::RuntimeApiResponse> make_ready_future(tp::RuntimeApiResponse response) {
  std::promise<tp::RuntimeApiResponse> promise;
  promise.set_value(std::move(response));
  return promise.get_future();
}

std::filesystem::path make_temp_db(std::string_view suffix) {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("task_orchestrator_operator_service_" + std::string(suffix) + "_" + std::to_string(unique));
  return directory / "operator_service.sqlite3";
}

tp::RuntimeApiResponse make_runtime_ok_response() {
  tp::RuntimeApiResponse response;
  response.set_ok(true);
  response.mutable_result()->set_ok(true);
  return response;
}

tp::RuntimeApiResponse make_runtime_error_response(std::string_view message, bool top_level_error) {
  tp::RuntimeApiResponse response;
  response.set_ok(!top_level_error);
  response.set_error_message(top_level_error ? std::string(message) : "");
  response.mutable_result()->set_ok(false);
  response.mutable_result()->set_error_message(std::string(message));
  return response;
}

tp::WorkflowRecord make_workflow_record(std::string_view workflow_id) {
  const auto now = unix_time_ms_now();
  tp::WorkflowRecord workflow;
  workflow.mutable_summary()->set_workflow_id(std::string(workflow_id));
  workflow.mutable_summary()->set_state(tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED);
  workflow.mutable_summary()->set_created_at_unix_ms(now - 1'000);
  workflow.mutable_summary()->set_updated_at_unix_ms(now);
  workflow.mutable_summary()->set_latest_plan_version(2);
  workflow.mutable_summary()->set_total_event_count(2);
  workflow.mutable_summary()->set_total_audit_entry_count(1);
  workflow.mutable_config()->set_id(std::string(workflow_id));
  auto* actor = workflow.mutable_config()->add_actors();
  actor->set_id("robot_1");
  actor->set_type("robot");
  actor->set_capacity(1);
  actor->add_capabilities("pick");

  auto* task = workflow.mutable_config()->add_tasks();
  task->set_id("pick-1");
  task->set_requested_time(now + 60'000);
  task->set_duration(300'000);
  task->set_deadline(now + 900'000);
  task->set_priority(5);
  task->set_mandatory(true);
  task->set_preemptible(false);
  task->add_preferred_actor_ids("robot_1");
  return workflow;
}

tp::WorkflowEventRecord make_event_record(std::int64_t sequence,
                                          std::string_view workflow_id,
                                          std::string_view detail) {
  tp::WorkflowEventRecord record;
  record.set_sequence(sequence);
  record.set_recorded_at_unix_ms(unix_time_ms_now() - ((3 - sequence) * 1'000));
  record.mutable_event()->set_workflow_id(std::string(workflow_id));
  record.mutable_event()->set_type(tp::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED);
  record.mutable_event()->set_detail(std::string(detail));
  return record;
}

tp::WorkflowPlanVersion make_plan_version(std::int64_t version, std::string_view workflow_id) {
  tp::WorkflowPlanVersion plan_version;
  plan_version.set_version(version);
  plan_version.set_recorded_at_unix_ms(unix_time_ms_now() - ((3 - version) * 1'000));
  plan_version.mutable_response()->set_ok(true);
  plan_version.mutable_response()->mutable_result()->set_ok(true);
  auto* assignment = plan_version.mutable_response()->mutable_result()->add_assignments();
  assignment->set_task_id(version == 1 ? "pick-1" : "pick-2");
  assignment->set_actor_id("robot_1");
  assignment->set_start_time(plan_version.recorded_at_unix_ms());
  assignment->set_end_time(plan_version.recorded_at_unix_ms() + 300'000);
  (void)workflow_id;
  return plan_version;
}

tp::AuditEntry make_audit_entry(std::int64_t sequence, std::string_view action, std::string_view detail) {
  tp::AuditEntry entry;
  entry.set_sequence(sequence);
  entry.set_recorded_at_unix_ms(unix_time_ms_now() - 500);
  entry.set_actor("dispatcher");
  entry.set_action(std::string(action));
  entry.set_detail(std::string(detail));
  return entry;
}

void seed_workflow(const std::shared_ptr<tcp::store::SqliteWorkflowStore>& store, std::string_view workflow_id) {
  const tp::WorkflowRecord workflow = make_workflow_record(workflow_id);
  store->upsert_workflow(workflow);
  store->write_event(workflow_id, make_event_record(1, workflow_id, "workflow stored"));
  store->write_event(workflow_id, make_event_record(2, workflow_id, "plan stored"));
  store->write_plan_version(workflow_id, make_plan_version(1, workflow_id));
  store->write_plan_version(workflow_id, make_plan_version(2, workflow_id));
  store->write_audit_entry(workflow_id, make_audit_entry(1, "seed", "seeded workflow"));
}

void seed_workflow_with_single_plan(const std::shared_ptr<tcp::store::SqliteWorkflowStore>& store,
                                    std::string_view workflow_id) {
  const tp::WorkflowRecord workflow = make_workflow_record(workflow_id);
  store->upsert_workflow(workflow);
  store->write_event(workflow_id, make_event_record(1, workflow_id, "workflow stored"));
  store->write_plan_version(workflow_id, make_plan_version(1, workflow_id));
  store->write_audit_entry(workflow_id, make_audit_entry(1, "seed", "seeded workflow"));
}

class FakeRuntimeService final : public tp::WorkflowRuntimeService {
 public:
  tp::RuntimeApiResponse submit_workflow(const tp::SubmitWorkflowRequest& request) override {
    ++submit_calls;
    last_submit_request = request;
    return submit_response;
  }

  tp::RuntimeApiResponse reorchestrate(const tp::ReorchestrateRequest& request) override {
    ++reorchestrate_calls;
    last_reorchestrate_request = request;
    return reorchestrate_response;
  }

  std::future<tp::RuntimeApiResponse> submit_workflow_async(const tp::SubmitWorkflowRequest& request) override {
    return make_ready_future(submit_workflow(request));
  }

  std::future<tp::RuntimeApiResponse> reorchestrate_async(const tp::ReorchestrateRequest& request) override {
    return make_ready_future(reorchestrate(request));
  }

  tp::WorkflowEventStream stream_submit_workflow(tp::SubmitWorkflowRequest) override { co_return; }

  tp::WorkflowEventStream stream_reorchestrate(tp::ReorchestrateRequest) override { co_return; }

  tp::RuntimeApiResponse submit_response = make_runtime_ok_response();
  tp::RuntimeApiResponse reorchestrate_response = make_runtime_ok_response();
  tp::SubmitWorkflowRequest last_submit_request;
  tp::ReorchestrateRequest last_reorchestrate_request;
  int submit_calls = 0;
  int reorchestrate_calls = 0;
};

class FakeControlPlaneService final : public tp::WorkflowControlPlaneService {
 public:
  explicit FakeControlPlaneService(std::shared_ptr<tcp::store::WorkflowStore> store) : store_(std::move(store)) {
    plan_diff_response_.set_ok(true);
    auto* before = plan_diff_response_.mutable_diff()->add_changed_assignments()->mutable_before();
    before->set_task_id("pick-1");
    before->set_actor_id("robot_1");
    before->set_start_time(100);
    auto* after = plan_diff_response_.mutable_diff()->mutable_changed_assignments(0)->mutable_after();
    after->set_task_id("pick-1");
    after->set_actor_id("robot_2");
    after->set_start_time(200);
  }

  tp::ListWorkflowsResponse list_workflows(const tp::ListWorkflowsRequest& request) override {
    ++list_calls;
    last_list_request = request;
    return store_->list_workflows(request);
  }

  tp::SearchWorkflowsResponse search_workflows(const tp::SearchWorkflowsRequest& request) override {
    ++search_calls;
    last_search_request = request;
    return store_->search_workflows(request);
  }

  tp::GetWorkflowResponse get_workflow(const tp::GetWorkflowRequest& request) override {
    tp::GetWorkflowResponse response;
    const auto workflow = store_->get_workflow(request.workflow_id());
    if (!workflow.has_value()) {
      response.set_ok(false);
      response.set_error_message("Workflow not found.");
      return response;
    }
    response.set_ok(true);
    *response.mutable_workflow() = *workflow;
    return response;
  }

  tp::GetWorkflowHistoryResponse get_workflow_history(const tp::GetWorkflowHistoryRequest& request) override {
    ++history_calls;
    last_history_request = request;
    tp::GetWorkflowHistoryResponse response;
    if (force_history_error) {
      response.set_ok(false);
      response.set_error_message("history unavailable");
      return response;
    }

    const auto workflow = store_->get_workflow(request.workflow_id());
    if (!workflow.has_value()) {
      response.set_ok(false);
      response.set_error_message("Workflow not found.");
      return response;
    }

    response.set_ok(true);
    *response.mutable_workflow() = *workflow;
    for (const auto& event :
         store_->list_events(request.workflow_id(), request.max_events() > 0 ? request.max_events() : 0)) {
      *response.add_events() = event;
    }
    for (const auto& plan_version : store_->list_plan_versions(
             request.workflow_id(), request.max_plan_versions() > 0 ? request.max_plan_versions() : 0)) {
      *response.add_plan_versions() = plan_version;
    }
    for (const auto& audit_entry : store_->list_audit_entries(
             request.workflow_id(), request.max_audit_entries() > 0 ? request.max_audit_entries() : 0)) {
      *response.add_audit_entries() = audit_entry;
    }
    return response;
  }

  tp::GetPlanDiffResponse get_plan_diff(const tp::GetPlanDiffRequest& request) override {
    ++diff_calls;
    last_diff_request = request;
    return plan_diff_response_;
  }

  tp::WorkflowActionResponse pause_workflow(const tp::PauseWorkflowRequest& request) override {
    ++pause_calls;
    last_pause_request = request;
    return set_state(request.workflow_id(), tp::pb::WORKFLOW_LIFECYCLE_STATE_PAUSED);
  }

  tp::WorkflowActionResponse resume_workflow(const tp::ResumeWorkflowRequest& request) override {
    ++resume_calls;
    last_resume_request = request;
    return set_state(request.workflow_id(), tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED);
  }

  tp::WorkflowActionResponse cancel_workflow(const tp::CancelWorkflowRequest& request) override {
    ++cancel_calls;
    last_cancel_request = request;
    return set_state(request.workflow_id(), tp::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED);
  }

  tp::WorkflowActionResponse apply_manual_intervention(const tp::ManualInterventionRequest& request) override {
    ++manual_calls;
    last_manual_request = request;
    tp::WorkflowActionResponse response;
    response.set_ok(true);
    const auto workflow = store_->get_workflow(request.workflow_id());
    if (workflow.has_value()) {
      *response.mutable_workflow() = *workflow;
    }
    return response;
  }

  int list_calls = 0;
  int search_calls = 0;
  int history_calls = 0;
  int diff_calls = 0;
  int pause_calls = 0;
  int resume_calls = 0;
  int cancel_calls = 0;
  int manual_calls = 0;
  bool force_history_error = false;
  tp::ListWorkflowsRequest last_list_request;
  tp::SearchWorkflowsRequest last_search_request;
  tp::GetWorkflowHistoryRequest last_history_request;
  tp::GetPlanDiffRequest last_diff_request;
  tp::PauseWorkflowRequest last_pause_request;
  tp::ResumeWorkflowRequest last_resume_request;
  tp::CancelWorkflowRequest last_cancel_request;
  tp::ManualInterventionRequest last_manual_request;

 private:
  tp::WorkflowActionResponse set_state(const std::string& workflow_id, tp::WorkflowLifecycleState state) {
    tp::WorkflowActionResponse response;
    const auto workflow = store_->get_workflow(workflow_id);
    if (!workflow.has_value()) {
      response.set_ok(false);
      response.set_error_message("Workflow not found.");
      return response;
    }

    tp::WorkflowRecord updated = *workflow;
    updated.mutable_summary()->set_state(state);
    updated.mutable_summary()->set_updated_at_unix_ms(unix_time_ms_now());
    store_->upsert_workflow(updated);
    response.set_ok(true);
    *response.mutable_workflow() = updated;
    return response;
  }

  std::shared_ptr<tcp::store::WorkflowStore> store_;
  tp::GetPlanDiffResponse plan_diff_response_;
};

TEST(OperatorServiceTest, DashboardUsesStoreHistoryDiffAndConnectorBindings) {
  const std::filesystem::path database_path = make_temp_db("dashboard");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-dashboard");
  seed_workflow(store, "wf-1");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  auto update_feed = tcp::service::make_in_memory_workflow_update_feed();
  registry->upsert_binding({.id = "webhook",
                            .kind = tcp::integration::ConnectorKind::WebhookSource,
                            .display_name = "Webhook",
                            .target = "/orders",
                            .enabled = true});
  registry->upsert_binding({.id = "schedule",
                            .kind = tcp::integration::ConnectorKind::ScheduleSource,
                            .display_name = "Schedule",
                            .target = "cron:*",
                            .enabled = true});
  registry->upsert_binding({.id = "queue",
                            .kind = tcp::integration::ConnectorKind::QueueConsumer,
                            .display_name = "Queue",
                            .target = "nats://orders",
                            .enabled = true});
  registry->upsert_binding({.id = "callback",
                            .kind = tcp::integration::ConnectorKind::OutboundCallback,
                            .display_name = "Callback",
                            .target = "https://cb",
                            .enabled = false});
  registry->upsert_binding({.id = "domain",
                            .kind = tcp::integration::ConnectorKind::DomainConnector,
                            .display_name = "Domain",
                            .target = "erp://sync",
                            .enabled = true});

  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  toa::OperatorService service(runtime, control_plane, store, registry, update_feed);

  const tp::GetOperatorDashboardResponse dashboard = service.get_dashboard({});
  EXPECT_TRUE(dashboard.ok());
  EXPECT_EQ("wf-1", dashboard.selected_workflow_id());
  EXPECT_EQ(5, dashboard.connectors_size());
  EXPECT_EQ(5, dashboard.stats().connectors_tracked());
  EXPECT_EQ(1, control_plane.list_calls);
  EXPECT_EQ(0, control_plane.search_calls);
  EXPECT_EQ(1, control_plane.history_calls);
  EXPECT_EQ(1, control_plane.diff_calls);
  EXPECT_EQ(32, control_plane.last_list_request.page_size());
  EXPECT_EQ(32, control_plane.last_history_request.max_events());
  EXPECT_EQ(32, control_plane.last_history_request.max_plan_versions());
  EXPECT_EQ(32, control_plane.last_history_request.max_audit_entries());
  EXPECT_EQ("Webhook source", dashboard.connectors(0).kind());
  EXPECT_EQ("Schedule source", dashboard.connectors(1).kind());
  EXPECT_EQ("Queue consumer", dashboard.connectors(2).kind());
  EXPECT_EQ("Outbound callback", dashboard.connectors(3).kind());
  EXPECT_EQ("Domain connector", dashboard.connectors(4).kind());

  tp::GetOperatorDashboardRequest search_request;
  search_request.set_workflow_query("wf");
  search_request.set_workflow_page_size(4);
  const tp::GetOperatorDashboardResponse searched_dashboard = service.get_dashboard(search_request);
  EXPECT_TRUE(searched_dashboard.ok());
  EXPECT_EQ(1, control_plane.search_calls);
  EXPECT_EQ("wf", control_plane.last_search_request.query());
  EXPECT_EQ(4, control_plane.last_search_request.page_size());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(OperatorServiceTest, DashboardHandlesEmptySelectionAndHistoryFailures) {
  const std::filesystem::path empty_database_path = make_temp_db("empty");
  std::filesystem::remove_all(empty_database_path.parent_path());
  auto empty_store = std::make_shared<tcp::store::SqliteWorkflowStore>(empty_database_path, "boot-empty");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  auto update_feed = tcp::service::make_in_memory_workflow_update_feed();
  FakeRuntimeService empty_runtime;
  FakeControlPlaneService empty_control_plane(empty_store);
  toa::OperatorService empty_service(empty_runtime, empty_control_plane, empty_store, registry, update_feed);

  const tp::GetOperatorDashboardResponse empty_dashboard = empty_service.get_dashboard({});
  EXPECT_TRUE(empty_dashboard.ok());
  EXPECT_TRUE(empty_dashboard.selected_workflow_id().empty());
  EXPECT_EQ(0, empty_control_plane.history_calls);

  const std::filesystem::path database_path = make_temp_db("history_error");
  std::filesystem::remove_all(database_path.parent_path());
  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-history-error");
  seed_workflow(store, "wf-err");
  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  control_plane.force_history_error = true;
  toa::OperatorService service(runtime, control_plane, store, registry, update_feed);

  tp::GetOperatorDashboardRequest request;
  request.set_selected_workflow_id("wf-err");
  const tp::GetOperatorDashboardResponse dashboard = service.get_dashboard(request);
  EXPECT_FALSE(dashboard.ok());
  EXPECT_EQ("history unavailable", dashboard.error_message());

  std::filesystem::remove_all(empty_database_path.parent_path());
  std::filesystem::remove_all(database_path.parent_path());
}

TEST(OperatorServiceTest, DashboardUpdateRefreshesMatchingWorkflowAndPropagatesHistoryErrors) {
  const std::filesystem::path database_path = make_temp_db("dashboard_update");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-dashboard-update");
  seed_workflow(store, "wf-1");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  auto update_feed = tcp::service::make_in_memory_workflow_update_feed();
  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  toa::OperatorService service(runtime, control_plane, store, registry, update_feed);

  tp::GetOperatorDashboardRequest request;
  request.set_selected_workflow_id("wf-1");
  request.set_workflow_page_size(5);
  request.set_max_events(3);
  request.set_max_plan_versions(3);
  request.set_max_audit_entries(2);

  tp::OperatorDashboardNotification notification;
  notification.event_id = 7;
  notification.workflow_id = "wf-1";

  const tp::OperatorDashboardUpdate update = service.get_dashboard_update(request, notification);
  EXPECT_TRUE(update.ok());
  EXPECT_EQ("wf-1", update.selected_workflow_id());
  EXPECT_EQ(1, control_plane.history_calls);
  EXPECT_EQ(1, control_plane.diff_calls);
  EXPECT_EQ("wf-1", update.selected_workflow().workflow().summary().workflow_id());
  EXPECT_EQ(2, update.selected_workflow().plan_versions_size());

  notification.workflow_id = "wf-2";
  const tp::OperatorDashboardUpdate other_workflow_update = service.get_dashboard_update(request, notification);
  EXPECT_TRUE(other_workflow_update.ok());
  EXPECT_EQ(1, control_plane.history_calls);
  EXPECT_EQ(1, control_plane.diff_calls);
  EXPECT_FALSE(other_workflow_update.has_selected_workflow());

  control_plane.force_history_error = true;
  notification.workflow_id = "wf-1";
  const tp::OperatorDashboardUpdate failed_update = service.get_dashboard_update(request, notification);
  EXPECT_FALSE(failed_update.ok());
  EXPECT_EQ("history unavailable", failed_update.error_message());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(OperatorServiceTest, DashboardSkipsPlanDiffWhenOnlyOnePlanVersionExists) {
  const std::filesystem::path database_path = make_temp_db("single_plan");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-single-plan");
  seed_workflow_with_single_plan(store, "wf-1");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  auto update_feed = tcp::service::make_in_memory_workflow_update_feed();
  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  toa::OperatorService service(runtime, control_plane, store, registry, update_feed);

  tp::GetOperatorDashboardRequest request;
  request.set_selected_workflow_id("wf-1");
  const tp::GetOperatorDashboardResponse dashboard = service.get_dashboard(request);

  EXPECT_TRUE(dashboard.ok());
  EXPECT_EQ("wf-1", dashboard.selected_workflow_id());
  EXPECT_EQ(1, dashboard.selected_workflow().plan_versions_size());
  EXPECT_TRUE(dashboard.selected_plan_diff().ok());
  EXPECT_EQ(1, control_plane.history_calls);
  EXPECT_EQ(0, control_plane.diff_calls);

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(OperatorServiceTest, WorkflowAndTaskMutationsForwardRuntimeRequestsAndWriteAuditEntries) {
  const std::filesystem::path database_path = make_temp_db("mutations");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-mutations");
  seed_workflow(store, "wf-1");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  auto update_feed = tcp::service::make_in_memory_workflow_update_feed();
  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  toa::OperatorService service(runtime, control_plane, store, registry, update_feed);

  EXPECT_FALSE(service.upsert_workflow({}).ok());

  runtime.submit_response = make_runtime_error_response("submit failed", true);
  tp::UpsertOperatorWorkflowRequest workflow_request;
  workflow_request.mutable_config()->set_id("wf-1");
  workflow_request.mutable_config()->add_tasks()->set_id("pick-1");
  workflow_request.set_idempotency_key("idem-workflow");
  const tp::OperatorMutationResponse workflow_response = service.upsert_workflow(workflow_request);
  EXPECT_FALSE(workflow_response.ok());
  EXPECT_EQ("submit failed", workflow_response.error_message());
  EXPECT_EQ(1, runtime.submit_calls);
  ASSERT_TRUE(runtime.last_submit_request.has_replace_existing());
  EXPECT_TRUE(runtime.last_submit_request.replace_existing());
  ASSERT_TRUE(runtime.last_submit_request.has_idempotency_key());
  EXPECT_EQ("idem-workflow", runtime.last_submit_request.idempotency_key());

  const auto workflow_audits = store->list_audit_entries("wf-1");
  ASSERT_GE(workflow_audits.size(), 2U);
  EXPECT_EQ("operator_upsert_workflow", workflow_audits.back().action());
  EXPECT_EQ("operator_ui", workflow_audits.back().actor());
  const auto workflow_update = service.wait_for_dashboard_update(0, std::chrono::milliseconds(1));
  ASSERT_TRUE(workflow_update.has_value());
  EXPECT_EQ("wf-1", workflow_update->workflow_id);
  EXPECT_EQ(workflow_update->event_id, service.latest_dashboard_event_id());

  EXPECT_FALSE(service.upsert_task({}).ok());

  runtime.submit_response = make_runtime_error_response("result failed", false);
  tp::UpsertOperatorTaskRequest replace_task_request;
  replace_task_request.set_workflow_id("wf-1");
  replace_task_request.mutable_task()->set_id("pick-1");
  replace_task_request.mutable_task()->set_priority(9);
  replace_task_request.set_idempotency_key("idem-task");
  const tp::OperatorMutationResponse replace_task_response = service.upsert_task(replace_task_request);
  EXPECT_FALSE(replace_task_response.ok());
  EXPECT_EQ("result failed", replace_task_response.error_message());
  ASSERT_TRUE(runtime.last_submit_request.has_idempotency_key());
  EXPECT_EQ("idem-task", runtime.last_submit_request.idempotency_key());
  EXPECT_EQ(9, runtime.last_submit_request.config().tasks(0).priority());

  tp::UpsertOperatorTaskRequest insert_task_request;
  insert_task_request.set_workflow_id("wf-1");
  insert_task_request.mutable_task()->set_id("pick-2");
  insert_task_request.set_note("add new task");
  runtime.submit_response = make_runtime_ok_response();
  const tp::OperatorMutationResponse insert_task_response = service.upsert_task(insert_task_request);
  EXPECT_TRUE(insert_task_response.ok());
  EXPECT_EQ(2, runtime.last_submit_request.config().tasks_size());

  tp::DeleteOperatorTaskRequest delete_task_request;
  delete_task_request.set_workflow_id("wf-1");
  delete_task_request.set_task_id("pick-1");
  delete_task_request.set_idempotency_key("idem-delete");
  const tp::OperatorMutationResponse delete_task_response = service.delete_task(delete_task_request);
  EXPECT_TRUE(delete_task_response.ok());
  EXPECT_EQ(0, runtime.last_submit_request.config().tasks_size());
  ASSERT_TRUE(runtime.last_submit_request.has_idempotency_key());
  EXPECT_EQ("idem-delete", runtime.last_submit_request.idempotency_key());

  tp::DeleteOperatorTaskRequest missing_task_request;
  missing_task_request.set_workflow_id("wf-1");
  missing_task_request.set_task_id("missing");
  EXPECT_FALSE(service.delete_task(missing_task_request).ok());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(OperatorServiceTest, ValidationErrorsAndMissingFeedsReturnStructuredResponses) {
  const std::filesystem::path database_path = make_temp_db("validation");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-validation");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  toa::OperatorService service(runtime, control_plane, store, registry, {});

  EXPECT_EQ(0U, service.latest_dashboard_event_id());
  EXPECT_FALSE(service.wait_for_dashboard_update(0, std::chrono::milliseconds(1)).has_value());

  tp::UpsertOperatorTaskRequest missing_task_id_request;
  missing_task_id_request.set_workflow_id("wf-1");
  const tp::OperatorMutationResponse missing_task_id_response = service.upsert_task(missing_task_id_request);
  EXPECT_FALSE(missing_task_id_response.ok());
  EXPECT_EQ("task.id is required.", missing_task_id_response.error_message());

  tp::UpsertOperatorTaskRequest missing_workflow_request;
  missing_workflow_request.set_workflow_id("wf-1");
  missing_workflow_request.mutable_task()->set_id("pick-1");
  const tp::OperatorMutationResponse missing_workflow_response = service.upsert_task(missing_workflow_request);
  EXPECT_FALSE(missing_workflow_response.ok());
  EXPECT_EQ("Workflow record not found.", missing_workflow_response.error_message());

  tp::DeleteOperatorTaskRequest missing_workflow_id_request;
  const tp::OperatorMutationResponse missing_workflow_id_response = service.delete_task(missing_workflow_id_request);
  EXPECT_FALSE(missing_workflow_id_response.ok());
  EXPECT_EQ("workflow_id is required.", missing_workflow_id_response.error_message());

  tp::DeleteOperatorTaskRequest missing_task_id_delete_request;
  missing_task_id_delete_request.set_workflow_id("wf-1");
  const tp::OperatorMutationResponse missing_task_id_delete_response =
      service.delete_task(missing_task_id_delete_request);
  EXPECT_FALSE(missing_task_id_delete_response.ok());
  EXPECT_EQ("task_id is required.", missing_task_id_delete_response.error_message());

  tp::DeleteOperatorTaskRequest missing_delete_workflow_request;
  missing_delete_workflow_request.set_workflow_id("wf-1");
  missing_delete_workflow_request.set_task_id("pick-1");
  const tp::OperatorMutationResponse missing_delete_workflow_response =
      service.delete_task(missing_delete_workflow_request);
  EXPECT_FALSE(missing_delete_workflow_response.ok());
  EXPECT_EQ("Workflow record not found.", missing_delete_workflow_response.error_message());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(OperatorServiceTest, LifecycleActionsUseControlPlaneDefaultsAndCustomReasons) {
  const std::filesystem::path database_path = make_temp_db("actions");
  std::filesystem::remove_all(database_path.parent_path());

  auto store = std::make_shared<tcp::store::SqliteWorkflowStore>(database_path, "boot-actions");
  seed_workflow(store, "wf-1");
  auto registry = tcp::integration::make_in_memory_connector_registry();
  auto update_feed = tcp::service::make_in_memory_workflow_update_feed();
  FakeRuntimeService runtime;
  FakeControlPlaneService control_plane(store);
  toa::OperatorService service(runtime, control_plane, store, registry, update_feed);

  tp::OperatorWorkflowActionRequest pause_request;
  pause_request.set_workflow_id("wf-1");
  pause_request.set_actor("alice");
  EXPECT_TRUE(service.pause_workflow(pause_request).ok());
  EXPECT_EQ("Workflow paused from the operator console.", control_plane.last_pause_request.reason());
  EXPECT_EQ("alice", control_plane.last_pause_request.actor());

  tp::OperatorWorkflowActionRequest resume_request;
  resume_request.set_workflow_id("wf-1");
  resume_request.set_reason("resume now");
  EXPECT_TRUE(service.resume_workflow(resume_request).ok());
  EXPECT_EQ("resume now", control_plane.last_resume_request.reason());

  tp::OperatorWorkflowActionRequest cancel_request;
  cancel_request.set_workflow_id("wf-1");
  EXPECT_TRUE(service.cancel_workflow(cancel_request).ok());
  EXPECT_EQ("Workflow cancelled from the operator console.", control_plane.last_cancel_request.reason());

  tp::ManualInterventionRequest intervention_request;
  intervention_request.set_workflow_id("wf-1");
  intervention_request.set_note("hold the lane");
  intervention_request.set_trigger_reorchestration(true);
  EXPECT_TRUE(service.apply_manual_intervention(intervention_request).ok());
  EXPECT_EQ("hold the lane", control_plane.last_manual_request.note());
  EXPECT_TRUE(control_plane.last_manual_request.trigger_reorchestration());

  std::filesystem::remove_all(database_path.parent_path());
}

}  // namespace
