#include "operator/operator_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace task_orchestrator::app::operator_api {
namespace {

constexpr std::int32_t kDefaultWorkflowPageSize = 32;
constexpr std::int32_t kDefaultHistoryLimit = 32;
constexpr std::string_view kDefaultOperatorActor = "operator_ui";

std::int64_t unix_time_ms_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string resolve_actor(std::string actor) {
  return actor.empty() ? std::string(kDefaultOperatorActor) : std::move(actor);
}

std::string connector_kind_label(const control_plane::integration::ConnectorKind kind) {
  using control_plane::integration::ConnectorKind;

  switch (kind) {
    case ConnectorKind::WebhookSource:
      return "Webhook source";
    case ConnectorKind::ScheduleSource:
      return "Schedule source";
    case ConnectorKind::QueueConsumer:
      return "Queue consumer";
    case ConnectorKind::OutboundCallback:
      return "Outbound callback";
    case ConnectorKind::DomainConnector:
    default:
      return "Domain connector";
  }
}

std::string runtime_error_message(const protocol::RuntimeApiResponse& response) {
  if (!response.ok() && !response.error_message().empty()) {
    return response.error_message();
  }
  if (!response.result().ok() && !response.result().error_message().empty()) {
    return response.result().error_message();
  }
  return {};
}

std::string action_reason_or_default(std::string reason, std::string_view fallback) {
  return reason.empty() ? std::string(fallback) : std::move(reason);
}

}  // namespace

OperatorService::OperatorService(protocol::WorkflowRuntimeService& runtime_service,
                                 protocol::WorkflowControlPlaneService& control_plane_service,
                                 std::shared_ptr<control_plane::store::WorkflowStore> workflow_store,
                                 std::shared_ptr<control_plane::integration::ConnectorRegistry> connector_registry,
                                 std::shared_ptr<control_plane::service::WorkflowUpdateFeed> workflow_update_feed)
    : runtime_service_(runtime_service),
      control_plane_service_(control_plane_service),
      workflow_store_(std::move(workflow_store)),
      connector_registry_(std::move(connector_registry)),
      workflow_update_feed_(std::move(workflow_update_feed)) {}

protocol::GetOperatorDashboardResponse OperatorService::get_dashboard(
    const protocol::GetOperatorDashboardRequest& request) {
  return build_dashboard(request.selected_workflow_id(),
                         request.workflow_query(),
                         request.auth(),
                         request.workflow_page_size(),
                         request.max_events(),
                         request.max_plan_versions(),
                         request.max_audit_entries());
}

protocol::OperatorDashboardUpdate OperatorService::get_dashboard_update(
    const protocol::GetOperatorDashboardRequest& request, const protocol::OperatorDashboardNotification& notification) {
  return build_dashboard_update(request, notification);
}

protocol::OperatorMutationResponse OperatorService::upsert_workflow(
    const protocol::UpsertOperatorWorkflowRequest& request) {
  if (request.config().id().empty()) {
    return build_mutation_response({}, false, "Workflow id is required.");
  }

  protocol::SubmitWorkflowRequest submit_request;
  *submit_request.mutable_config() = request.config();
  *submit_request.mutable_auth() = request.auth();
  submit_request.set_replace_existing(true);
  if (request.has_idempotency_key()) {
    submit_request.set_idempotency_key(request.idempotency_key());
  }

  const protocol::RuntimeApiResponse response = runtime_service_.submit_workflow(submit_request);
  const std::string error_message = runtime_error_message(response);
  append_audit_entry(
      request.config().id(),
      request.actor(),
      "operator_upsert_workflow",
      request.note().empty() ? "Workflow definition updated from the operator console." : request.note());
  return build_mutation_response(request.config().id(), error_message.empty(), error_message);
}

protocol::OperatorMutationResponse OperatorService::upsert_task(const protocol::UpsertOperatorTaskRequest& request) {
  if (request.workflow_id().empty()) {
    return build_mutation_response({}, false, "workflow_id is required.");
  }
  if (request.task().id().empty()) {
    return build_mutation_response(request.workflow_id(), false, "task.id is required.");
  }

  const auto workflow = workflow_store_->get_workflow(request.workflow_id());
  if (!workflow.has_value()) {
    return build_mutation_response(request.workflow_id(), false, "Workflow record not found.");
  }

  protocol::pb::WorkflowConfig updated_config = workflow->config();
  auto* tasks = updated_config.mutable_tasks();
  const auto existing_task = std::ranges::find(*tasks, request.task().id(), &protocol::pb::TaskConfig::id);
  const bool replacing_existing = existing_task != tasks->end();
  if (replacing_existing) {
    *existing_task = request.task();
  } else {
    *updated_config.add_tasks() = request.task();
  }

  protocol::SubmitWorkflowRequest submit_request;
  *submit_request.mutable_config() = updated_config;
  *submit_request.mutable_auth() = request.auth();
  submit_request.set_replace_existing(true);
  if (request.has_idempotency_key()) {
    submit_request.set_idempotency_key(request.idempotency_key());
  }

  const protocol::RuntimeApiResponse response = runtime_service_.submit_workflow(submit_request);
  const std::string error_message = runtime_error_message(response);
  const std::string task_action = replacing_existing ? "operator_update_task" : "operator_insert_task";
  const std::string default_detail = replacing_existing ? "Task schedule updated from the operator console."
                                                        : "Task schedule inserted from the operator console.";
  append_audit_entry(request.workflow_id(),
                     request.actor(),
                     task_action,
                     request.note().empty() ? default_detail + " task_id=" + request.task().id() : request.note());
  return build_mutation_response(request.workflow_id(), error_message.empty(), error_message);
}

protocol::OperatorMutationResponse OperatorService::delete_task(const protocol::DeleteOperatorTaskRequest& request) {
  if (request.workflow_id().empty()) {
    return build_mutation_response({}, false, "workflow_id is required.");
  }
  if (request.task_id().empty()) {
    return build_mutation_response(request.workflow_id(), false, "task_id is required.");
  }

  const auto workflow = workflow_store_->get_workflow(request.workflow_id());
  if (!workflow.has_value()) {
    return build_mutation_response(request.workflow_id(), false, "Workflow record not found.");
  }

  protocol::pb::WorkflowConfig updated_config = workflow->config();
  auto* tasks = updated_config.mutable_tasks();
  const auto removed_task = std::ranges::find(*tasks, request.task_id(), &protocol::pb::TaskConfig::id);
  if (removed_task == tasks->end()) {
    return build_mutation_response(request.workflow_id(), false, "Task record not found.");
  }
  tasks->erase(removed_task);

  protocol::SubmitWorkflowRequest submit_request;
  *submit_request.mutable_config() = updated_config;
  *submit_request.mutable_auth() = request.auth();
  submit_request.set_replace_existing(true);
  if (request.has_idempotency_key()) {
    submit_request.set_idempotency_key(request.idempotency_key());
  }

  const protocol::RuntimeApiResponse response = runtime_service_.submit_workflow(submit_request);
  const std::string error_message = runtime_error_message(response);
  append_audit_entry(
      request.workflow_id(),
      request.actor(),
      "operator_delete_task",
      request.note().empty() ? "Task removed from the operator console. task_id=" + request.task_id() : request.note());
  return build_mutation_response(request.workflow_id(), error_message.empty(), error_message);
}

protocol::OperatorMutationResponse OperatorService::pause_workflow(
    const protocol::OperatorWorkflowActionRequest& request) {
  protocol::PauseWorkflowRequest pause_request;
  pause_request.set_workflow_id(request.workflow_id());
  pause_request.set_actor(request.actor());
  pause_request.set_reason(action_reason_or_default(request.reason(), "Workflow paused from the operator console."));
  const protocol::WorkflowActionResponse response = control_plane_service_.pause_workflow(pause_request);
  return build_mutation_response(request.workflow_id(), response.ok(), response.error_message());
}

protocol::OperatorMutationResponse OperatorService::resume_workflow(
    const protocol::OperatorWorkflowActionRequest& request) {
  protocol::ResumeWorkflowRequest resume_request;
  resume_request.set_workflow_id(request.workflow_id());
  resume_request.set_actor(request.actor());
  resume_request.set_reason(action_reason_or_default(request.reason(), "Workflow resumed from the operator console."));
  const protocol::WorkflowActionResponse response = control_plane_service_.resume_workflow(resume_request);
  return build_mutation_response(request.workflow_id(), response.ok(), response.error_message());
}

protocol::OperatorMutationResponse OperatorService::cancel_workflow(
    const protocol::OperatorWorkflowActionRequest& request) {
  protocol::CancelWorkflowRequest cancel_request;
  cancel_request.set_workflow_id(request.workflow_id());
  cancel_request.set_actor(request.actor());
  cancel_request.set_reason(
      action_reason_or_default(request.reason(), "Workflow cancelled from the operator console."));
  const protocol::WorkflowActionResponse response = control_plane_service_.cancel_workflow(cancel_request);
  return build_mutation_response(request.workflow_id(), response.ok(), response.error_message());
}

protocol::OperatorMutationResponse OperatorService::apply_manual_intervention(
    const protocol::ManualInterventionRequest& request) {
  const protocol::WorkflowActionResponse response = control_plane_service_.apply_manual_intervention(request);
  return build_mutation_response(request.workflow_id(), response.ok(), response.error_message());
}

std::uint64_t OperatorService::latest_dashboard_event_id() const {
  return workflow_update_feed_ ? workflow_update_feed_->latest_event_id() : 0;
}

std::optional<protocol::OperatorDashboardNotification> OperatorService::wait_for_dashboard_update(
    const std::uint64_t after_event_id, const std::chrono::milliseconds timeout) {
  if (!workflow_update_feed_) {
    return std::nullopt;
  }
  const auto update = workflow_update_feed_->wait_for_update(after_event_id, timeout);
  if (!update.has_value()) {
    return std::nullopt;
  }
  return protocol::OperatorDashboardNotification{
      .event_id = update->event_id,
      .workflow_id = update->workflow_id,
  };
}

void OperatorService::populate_dashboard_stats(protocol::OperatorDashboardStats* stats, const std::int64_t now) const {
  const auto metrics = workflow_store_->get_storage_metrics(
      now - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(24)).count());
  stats->set_recent_events_persisted(metrics.recent_event_count);
  stats->set_plan_versions_retained(metrics.retained_plan_versions);
  stats->set_workflows_tracked(metrics.tracked_workflows);
  stats->set_active_workflows(metrics.active_workflows);
  stats->set_connectors_tracked(static_cast<std::int64_t>(connector_registry_->list_bindings().size()));
}

void OperatorService::populate_connector_bindings(
    google::protobuf::RepeatedPtrField<protocol::OperatorConnectorBinding>* bindings) const {
  for (const auto& connector : connector_registry_->list_bindings()) {
    auto* binding = bindings->Add();
    binding->set_id(connector.id);
    binding->set_kind(connector_kind_label(connector.kind));
    binding->set_display_name(connector.display_name);
    binding->set_target(connector.target);
    binding->set_enabled(connector.enabled);
  }
}

void OperatorService::populate_workflow_summaries(
    google::protobuf::RepeatedPtrField<protocol::WorkflowSummary>* workflows,
    const std::string_view workflow_query,
    const std::int32_t workflow_page_size) const {
  if (workflow_query.empty()) {
    protocol::ListWorkflowsRequest list_request;
    list_request.set_page_size(workflow_page_size > 0 ? workflow_page_size : kDefaultWorkflowPageSize);
    const protocol::ListWorkflowsResponse list_response = control_plane_service_.list_workflows(list_request);
    workflows->CopyFrom(list_response.workflows());
    return;
  }

  protocol::SearchWorkflowsRequest search_request;
  search_request.set_query(std::string(workflow_query));
  search_request.set_page_size(workflow_page_size > 0 ? workflow_page_size : kDefaultWorkflowPageSize);
  const protocol::SearchWorkflowsResponse search_response = control_plane_service_.search_workflows(search_request);
  workflows->CopyFrom(search_response.workflows());
}

std::string OperatorService::resolve_selected_workflow_id(
    std::string selected_workflow_id, const google::protobuf::RepeatedPtrField<protocol::WorkflowSummary>& workflows) {
  if (selected_workflow_id.empty() && !workflows.empty()) {
    selected_workflow_id = workflows.Get(0).workflow_id();
  }
  return selected_workflow_id;
}

bool OperatorService::populate_selected_workflow(std::string_view workflow_id,
                                                 const std::int32_t max_events,
                                                 const std::int32_t max_plan_versions,
                                                 const std::int32_t max_audit_entries,
                                                 protocol::GetWorkflowHistoryResponse* history_response,
                                                 protocol::GetPlanDiffResponse* plan_diff_response,
                                                 std::string* error_message) const {
  if (workflow_id.empty()) {
    history_response->set_ok(true);
    plan_diff_response->set_ok(true);
    return true;
  }

  protocol::GetWorkflowHistoryRequest history_request;
  history_request.set_workflow_id(std::string(workflow_id));
  history_request.set_max_events(max_events > 0 ? max_events : kDefaultHistoryLimit);
  history_request.set_max_plan_versions(max_plan_versions > 0 ? max_plan_versions : kDefaultHistoryLimit);
  history_request.set_max_audit_entries(max_audit_entries > 0 ? max_audit_entries : kDefaultHistoryLimit);
  *history_response = control_plane_service_.get_workflow_history(history_request);
  if (!history_response->ok()) {
    *error_message = history_response->error_message();
    return false;
  }

  if (history_response->plan_versions_size() < 2) {
    plan_diff_response->set_ok(true);
    return true;
  }

  protocol::GetPlanDiffRequest diff_request;
  diff_request.set_workflow_id(std::string(workflow_id));
  diff_request.set_from_plan_version(
      history_response->plan_versions(history_response->plan_versions_size() - 2).version());
  diff_request.set_to_plan_version(
      history_response->plan_versions(history_response->plan_versions_size() - 1).version());
  *plan_diff_response = control_plane_service_.get_plan_diff(diff_request);
  return true;
}

protocol::GetOperatorDashboardResponse OperatorService::build_dashboard(
    std::string selected_workflow_id,
    const std::string_view workflow_query,
    const protocol::pb::ClientAuthContext& auth_context,
    const std::int32_t workflow_page_size,
    const std::int32_t max_events,
    const std::int32_t max_plan_versions,
    const std::int32_t max_audit_entries) {
  protocol::GetOperatorDashboardResponse response;
  const std::int64_t now = unix_time_ms_now();
  response.set_ok(true);
  response.set_server_time_unix_ms(now);
  populate_dashboard_stats(response.mutable_stats(), now);
  populate_connector_bindings(response.mutable_connectors());
  populate_workflow_summaries(response.mutable_workflows(), workflow_query, workflow_page_size);
  selected_workflow_id = resolve_selected_workflow_id(std::move(selected_workflow_id), response.workflows());
  response.set_selected_workflow_id(selected_workflow_id);

  if (selected_workflow_id.empty()) {
    return response;
  }

  std::string error_message;
  if (!populate_selected_workflow(selected_workflow_id,
                                  max_events,
                                  max_plan_versions,
                                  max_audit_entries,
                                  response.mutable_selected_workflow(),
                                  response.mutable_selected_plan_diff(),
                                  &error_message)) {
    response.set_ok(false);
    response.set_error_message(std::move(error_message));
  }

  (void)auth_context;
  return response;
}

protocol::OperatorDashboardUpdate OperatorService::build_dashboard_update(
    const protocol::GetOperatorDashboardRequest& request, const protocol::OperatorDashboardNotification& notification) {
  protocol::OperatorDashboardUpdate update;
  const std::int64_t now = unix_time_ms_now();
  update.set_ok(true);
  update.set_server_time_unix_ms(now);

  populate_dashboard_stats(update.mutable_stats(), now);
  populate_workflow_summaries(update.mutable_workflows(), request.workflow_query(), request.workflow_page_size());
  populate_connector_bindings(update.mutable_connectors());

  const std::string selected_workflow_id =
      resolve_selected_workflow_id(request.selected_workflow_id(), update.workflows());
  update.set_selected_workflow_id(selected_workflow_id);

  if (!selected_workflow_id.empty() && selected_workflow_id == notification.workflow_id) {
    std::string error_message;
    if (!populate_selected_workflow(selected_workflow_id,
                                    request.max_events(),
                                    request.max_plan_versions(),
                                    request.max_audit_entries(),
                                    update.mutable_selected_workflow(),
                                    update.mutable_selected_plan_diff(),
                                    &error_message)) {
      update.set_ok(false);
      update.set_error_message(std::move(error_message));
    }
  }

  return update;
}

protocol::OperatorMutationResponse OperatorService::build_mutation_response(std::string selected_workflow_id,
                                                                            const bool ok,
                                                                            std::string error_message) {
  protocol::OperatorMutationResponse response;
  response.set_ok(ok);
  response.set_error_message(error_message);
  *response.mutable_dashboard() = build_dashboard(std::move(selected_workflow_id),
                                                  {},
                                                  {},
                                                  kDefaultWorkflowPageSize,
                                                  kDefaultHistoryLimit,
                                                  kDefaultHistoryLimit,
                                                  kDefaultHistoryLimit);
  return response;
}

void OperatorService::append_audit_entry(std::string_view workflow_id,
                                         std::string actor,
                                         std::string action,
                                         std::string detail) {
  const auto workflow = workflow_store_->get_workflow(workflow_id);
  if (!workflow.has_value()) {
    return;
  }

  protocol::WorkflowRecord updated_workflow = *workflow;
  protocol::AuditEntry audit_entry;
  audit_entry.set_sequence(updated_workflow.summary().total_audit_entry_count() + 1);
  audit_entry.set_recorded_at_unix_ms(unix_time_ms_now());
  audit_entry.set_actor(resolve_actor(std::move(actor)));
  audit_entry.set_action(std::move(action));
  audit_entry.set_detail(std::move(detail));
  workflow_store_->write_audit_entry(workflow_id, audit_entry);
  updated_workflow.mutable_summary()->set_total_audit_entry_count(audit_entry.sequence());
  updated_workflow.mutable_summary()->set_updated_at_unix_ms(audit_entry.recorded_at_unix_ms());
  workflow_store_->upsert_workflow(updated_workflow);
  publish_dashboard_update(workflow_id);
}

void OperatorService::publish_dashboard_update(const std::string_view workflow_id) const {
  if (!workflow_update_feed_ || workflow_id.empty()) {
    return;
  }
  workflow_update_feed_->publish(std::string(workflow_id));
}

}  // namespace task_orchestrator::app::operator_api
