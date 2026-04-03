#include "control_plane/service/control_plan_service.hpp"

#include <chrono>
#include <future>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

#include "control_plane/service/plan_diff.hpp"
#include "utils/logger.hpp"
#include "utils/task_executor.hpp"

namespace task_orchestrator::control_plane::service {
namespace {

constexpr const char* kInternalRecoveryActor = "control_plane/recovery";
constexpr const char* kInternalControlActor = "control_plane";

std::int64_t unix_time_ms_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string fingerprint_proto(const google::protobuf::Message& message) {
  const std::string serialized = message.SerializeAsString();
  return std::to_string(std::hash<std::string>{}(serialized));
}

protocol::RuntimeApiResponse make_runtime_error(std::string message) {
  protocol::RuntimeApiResponse response;
  response.set_ok(false);
  response.mutable_result()->set_ok(false);
  response.mutable_result()->set_error_message(message);
  response.set_error_message(std::move(message));
  return response;
}

protocol::WorkflowEvent make_response_event(protocol::pb::WorkflowEventType event_type,
                                            std::string_view workflow_id,
                                            const protocol::RuntimeApiResponse& response,
                                            std::string_view detail) {
  protocol::WorkflowEvent event;
  event.set_type(event_type);
  event.set_workflow_id(std::string(workflow_id));
  event.set_detail(std::string(detail));
  *event.mutable_response() = response;
  return event;
}

protocol::RuntimeApiResponse consume_final_response(protocol::WorkflowEventStream event_stream) {
  protocol::RuntimeApiResponse final_response;
  for (const protocol::WorkflowEvent& event : event_stream) {
    if (event.has_response()) {
      final_response = event.response();
    }
  }
  return final_response;
}

protocol::WorkflowLifecycleState state_for_terminal_response(const protocol::RuntimeApiResponse& response) {
  if (!response.ok() || !response.result().ok()) {
    return protocol::pb::WORKFLOW_LIFECYCLE_STATE_FAILED;
  }
  return protocol::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED;
}

protocol::WorkflowLifecycleState state_for_event(const protocol::WorkflowEvent& event,
                                                 protocol::WorkflowLifecycleState current_state) {
  switch (event.type()) {
    case protocol::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED:
      return protocol::pb::WORKFLOW_LIFECYCLE_STATE_SUBMITTED;
    case protocol::pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED:
      return protocol::pb::WORKFLOW_LIFECYCLE_STATE_PLANNING;
    case protocol::pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED:
      return protocol::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED;
    case protocol::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED:
      return event.has_response() ? state_for_terminal_response(event.response()) : current_state;
    case protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED:
      return protocol::pb::WORKFLOW_LIFECYCLE_STATE_FAILED;
    case protocol::pb::WORKFLOW_EVENT_TYPE_RUNTIME_OVERRIDE_APPLIED:
    case protocol::pb::WORKFLOW_EVENT_TYPE_TASK_COMPLETED:
    case protocol::pb::WORKFLOW_EVENT_TYPE_UNSPECIFIED:
    default:
      return current_state;
  }
}

}  // namespace

ControlPlanService::ControlPlanService(std::unique_ptr<protocol::WorkflowRuntimeService> runtime_service,
                                       std::shared_ptr<store::WorkflowStore> workflow_store,
                                       std::shared_ptr<WorkflowUpdateFeed> workflow_update_feed,
                                       std::shared_ptr<integration::ConnectorRegistry> connector_registry)
    : runtime_service_(std::move(runtime_service)),
      workflow_store_(std::move(workflow_store)),
      workflow_update_feed_(std::move(workflow_update_feed)),
      connector_registry_(std::move(connector_registry)) {}

void ControlPlanService::recover_active_workflows(const protocol::pb::ClientAuthContext& auth_context) {
  auto logger = get_logger(LogLayer::Application);
  for (protocol::WorkflowRecord workflow : workflow_store_->list_all_workflows()) {
    switch (workflow.summary().state()) {
      case protocol::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED:
      case protocol::pb::WORKFLOW_LIFECYCLE_STATE_FAILED:
      case protocol::pb::WORKFLOW_LIFECYCLE_STATE_UNSPECIFIED:
        continue;
      default:
        break;
    }

    protocol::AuditEntry audit_entry;
    audit_entry.set_sequence(workflow.summary().total_audit_entry_count() + 1);
    audit_entry.set_recorded_at_unix_ms(unix_time_ms_now());
    audit_entry.set_actor(kInternalRecoveryActor);
    audit_entry.set_action("recover");
    audit_entry.set_detail("Rehydrating workflow state into runtime memory.");
    workflow_store_->write_audit_entry(workflow.summary().workflow_id(), audit_entry);
    workflow.mutable_summary()->set_total_audit_entry_count(audit_entry.sequence());
    workflow.mutable_summary()->set_updated_at_unix_ms(audit_entry.recorded_at_unix_ms());
    workflow_store_->upsert_workflow(workflow);
    publish_workflow_update(workflow.summary().workflow_id());

    protocol::SubmitWorkflowRequest replay_request;
    *replay_request.mutable_config() = workflow.config();
    *replay_request.mutable_auth() = auth_context;
    replay_request.set_replace_existing(true);
    const protocol::RuntimeApiResponse replay_response = runtime_service_->submit_workflow(replay_request);

    {
      std::scoped_lock lock(hydrated_mutex_);
      hydrated_workflows_.insert(workflow.summary().workflow_id());
    }

    if (!replay_response.ok()) {
      logger->warn("Recovery replay for workflow '{}' failed: {}",
                   workflow.summary().workflow_id(),
                   replay_response.error_message());
    }
  }
}

protocol::RuntimeApiResponse ControlPlanService::submit_workflow(const protocol::SubmitWorkflowRequest& request) {
  return consume_final_response(stream_submit_workflow(request));
}

protocol::RuntimeApiResponse ControlPlanService::reorchestrate(const protocol::ReorchestrateRequest& request) {
  return consume_final_response(stream_reorchestrate(request));
}

std::future<protocol::RuntimeApiResponse> ControlPlanService::submit_workflow_async(
    const protocol::SubmitWorkflowRequest& request) {
  return get_shared_task_executor().try_submit([this, request]() { return submit_workflow(request); });
}

std::future<protocol::RuntimeApiResponse> ControlPlanService::reorchestrate_async(
    const protocol::ReorchestrateRequest& request) {
  return get_shared_task_executor().try_submit([this, request]() { return reorchestrate(request); });
}

protocol::WorkflowEventStream ControlPlanService::stream_submit_workflow(protocol::SubmitWorkflowRequest request) {
  const std::string workflow_id = request.config().id();
  const std::string idempotency_key = request.has_idempotency_key() ? request.idempotency_key() : std::string{};
  const std::string request_fingerprint = fingerprint_proto(request);

  if (!idempotency_key.empty()) {
    if (const auto existing = workflow_store_->get_idempotency_record(idempotency_key); existing.has_value()) {
      if (existing->request_fingerprint() != request_fingerprint) {
        const protocol::RuntimeApiResponse response =
            make_runtime_error("Idempotency key was reused for a different request payload.");
        co_yield make_response_event(
            protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, response, response.error_message());
        co_return;
      }
      co_yield make_response_event(protocol::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED,
                                   workflow_id,
                                   existing->cached_response(),
                                   "Idempotent replay served from durable control plane.");
      co_return;
    }
  }

  const std::int64_t now = unix_time_ms_now();
  protocol::WorkflowRecord record;
  const bool replacing_existing = request.has_replace_existing() && request.replace_existing();
  if (replacing_existing) {
    if (const auto existing_record = workflow_store_->get_workflow(workflow_id); existing_record.has_value()) {
      record = *existing_record;
    }
  }
  if (record.summary().workflow_id().empty()) {
    record.mutable_summary()->set_workflow_id(workflow_id);
    record.mutable_summary()->set_created_at_unix_ms(now);
  }
  record.mutable_summary()->set_state(protocol::pb::WORKFLOW_LIFECYCLE_STATE_SUBMITTED);
  record.mutable_summary()->set_updated_at_unix_ms(now);
  record.mutable_summary()->clear_last_error_message();
  *record.mutable_config() = request.config();
  workflow_store_->upsert_workflow(record);
  publish_workflow_update(workflow_id);

  {
    std::scoped_lock lock(hydrated_mutex_);
    hydrated_workflows_.insert(workflow_id);
  }

  for (const protocol::WorkflowEvent& event : runtime_service_->stream_submit_workflow(request)) {
    const std::int64_t recorded_at = unix_time_ms_now();
    record.mutable_summary()->set_updated_at_unix_ms(recorded_at);
    record.mutable_summary()->set_total_event_count(record.summary().total_event_count() + 1);
    record.mutable_summary()->set_state(state_for_event(event, record.summary().state()));
    if (event.has_response()) {
      *record.mutable_latest_response() = event.response();
      if (!event.response().ok()) {
        record.mutable_summary()->set_last_error_message(event.response().error_message());
      } else if (!event.response().result().ok()) {
        record.mutable_summary()->set_last_error_message(event.response().result().error_message());
      } else {
        record.mutable_summary()->clear_last_error_message();
        protocol::WorkflowPlanVersion plan_version;
        plan_version.set_version(record.summary().latest_plan_version() + 1);
        plan_version.set_recorded_at_unix_ms(recorded_at);
        *plan_version.mutable_response() = event.response();
        workflow_store_->write_plan_version(workflow_id, plan_version);
        record.mutable_summary()->set_latest_plan_version(plan_version.version());
      }
      if (!idempotency_key.empty()) {
        protocol::IdempotencyRecord idempotency_record;
        idempotency_record.set_key(idempotency_key);
        idempotency_record.set_workflow_id(workflow_id);
        idempotency_record.set_request_fingerprint(request_fingerprint);
        idempotency_record.set_recorded_at_unix_ms(recorded_at);
        *idempotency_record.mutable_cached_response() = event.response();
        workflow_store_->put_idempotency_record(idempotency_record);
      }
    }

    protocol::WorkflowEventRecord event_record;
    event_record.set_sequence(record.summary().total_event_count());
    event_record.set_recorded_at_unix_ms(recorded_at);
    *event_record.mutable_event() = event;
    workflow_store_->write_event(workflow_id, event_record);
    workflow_store_->upsert_workflow(record);
    publish_workflow_update(workflow_id);
    co_yield event;
  }
}

protocol::WorkflowEventStream ControlPlanService::stream_reorchestrate(protocol::ReorchestrateRequest request) {
  const std::string& workflow_id = request.workflow_id();
  const auto stored_workflow = workflow_store_->get_workflow(workflow_id);
  if (!stored_workflow.has_value()) {
    const protocol::RuntimeApiResponse response =
        make_runtime_error("Workflow record was not found in the control plane.");
    co_yield make_response_event(
        protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, response, response.error_message());
    co_return;
  }

  protocol::WorkflowRecord record = *stored_workflow;
  if (record.summary().state() == protocol::pb::WORKFLOW_LIFECYCLE_STATE_PAUSED) {
    const protocol::RuntimeApiResponse response =
        make_runtime_error("Workflow is paused. Resume it before re-orchestrating.");
    co_yield make_response_event(
        protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, response, response.error_message());
    co_return;
  }
  if (record.summary().state() == protocol::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED) {
    const protocol::RuntimeApiResponse response =
        make_runtime_error("Workflow is cancelled and cannot be re-orchestrated.");
    co_yield make_response_event(
        protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, response, response.error_message());
    co_return;
  }
  if (request.has_expected_plan_version() &&
      request.expected_plan_version() != record.summary().latest_plan_version()) {
    const protocol::RuntimeApiResponse response =
        make_runtime_error("Expected plan version does not match the durable control-plane record.");
    co_yield make_response_event(
        protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, response, response.error_message());
    co_return;
  }

  std::optional<protocol::RuntimeApiResponse> replay_failure_response;
  {
    std::scoped_lock lock(hydrated_mutex_);
    if (!hydrated_workflows_.contains(workflow_id)) {
      protocol::SubmitWorkflowRequest replay_request;
      *replay_request.mutable_config() = record.config();
      *replay_request.mutable_auth() = request.auth();
      replay_request.set_replace_existing(true);
      const protocol::RuntimeApiResponse replay_response = runtime_service_->submit_workflow(replay_request);
      if (!replay_response.ok()) {
        replay_failure_response = replay_response;
      } else {
        hydrated_workflows_.insert(workflow_id);
      }
    }
  }
  if (replay_failure_response.has_value()) {
    const protocol::RuntimeApiResponse& replay_response = *replay_failure_response;
    co_yield make_response_event(protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED,
                                 workflow_id,
                                 replay_response,
                                 replay_response.error_message());
    co_return;
  }

  const std::string idempotency_key = request.has_idempotency_key() ? request.idempotency_key() : std::string{};
  const std::string request_fingerprint = fingerprint_proto(request);
  if (!idempotency_key.empty()) {
    if (const auto existing = workflow_store_->get_idempotency_record(idempotency_key); existing.has_value()) {
      if (existing->request_fingerprint() != request_fingerprint) {
        const protocol::RuntimeApiResponse response =
            make_runtime_error("Idempotency key was reused for a different re-orchestration payload.");
        co_yield make_response_event(
            protocol::pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, response, response.error_message());
        co_return;
      }
      co_yield make_response_event(protocol::pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED,
                                   workflow_id,
                                   existing->cached_response(),
                                   "Idempotent replay served from durable control plane.");
      co_return;
    }
  }

  for (const protocol::WorkflowEvent& event : runtime_service_->stream_reorchestrate(request)) {
    const std::int64_t recorded_at = unix_time_ms_now();
    record.mutable_summary()->set_updated_at_unix_ms(recorded_at);
    record.mutable_summary()->set_total_event_count(record.summary().total_event_count() + 1);
    record.mutable_summary()->set_state(state_for_event(event, record.summary().state()));

    if (event.has_response()) {
      *record.mutable_latest_response() = event.response();
      if (!event.response().ok()) {
        record.mutable_summary()->set_last_error_message(event.response().error_message());
      } else if (!event.response().result().ok()) {
        record.mutable_summary()->set_last_error_message(event.response().result().error_message());
      } else {
        record.mutable_summary()->clear_last_error_message();
        protocol::WorkflowPlanVersion plan_version;
        plan_version.set_version(record.summary().latest_plan_version() + 1);
        plan_version.set_recorded_at_unix_ms(recorded_at);
        *plan_version.mutable_response() = event.response();
        workflow_store_->write_plan_version(workflow_id, plan_version);
        record.mutable_summary()->set_latest_plan_version(plan_version.version());
      }
      if (!idempotency_key.empty()) {
        protocol::IdempotencyRecord idempotency_record;
        idempotency_record.set_key(idempotency_key);
        idempotency_record.set_workflow_id(workflow_id);
        idempotency_record.set_request_fingerprint(request_fingerprint);
        idempotency_record.set_recorded_at_unix_ms(recorded_at);
        *idempotency_record.mutable_cached_response() = event.response();
        workflow_store_->put_idempotency_record(idempotency_record);
      }
    }

    protocol::WorkflowEventRecord event_record;
    event_record.set_sequence(record.summary().total_event_count());
    event_record.set_recorded_at_unix_ms(recorded_at);
    *event_record.mutable_event() = event;
    workflow_store_->write_event(workflow_id, event_record);
    workflow_store_->upsert_workflow(record);
    publish_workflow_update(workflow_id);
    co_yield event;
  }
}

protocol::ListWorkflowsResponse ControlPlanService::list_workflows(const protocol::ListWorkflowsRequest& request) {
  return workflow_store_->list_workflows(request);
}

protocol::SearchWorkflowsResponse ControlPlanService::search_workflows(
    const protocol::SearchWorkflowsRequest& request) {
  return workflow_store_->search_workflows(request);
}

protocol::GetWorkflowResponse ControlPlanService::get_workflow(const protocol::GetWorkflowRequest& request) {
  protocol::GetWorkflowResponse response;
  if (const auto workflow = workflow_store_->get_workflow(request.workflow_id()); workflow.has_value()) {
    response.set_ok(true);
    *response.mutable_workflow() = *workflow;
    return response;
  }
  response.set_ok(false);
  response.set_error_message("Workflow record not found.");
  return response;
}

protocol::GetWorkflowHistoryResponse ControlPlanService::get_workflow_history(
    const protocol::GetWorkflowHistoryRequest& request) {
  protocol::GetWorkflowHistoryResponse response;
  if (const auto workflow = workflow_store_->get_workflow(request.workflow_id()); workflow.has_value()) {
    response.set_ok(true);
    *response.mutable_workflow() = *workflow;
    for (const protocol::WorkflowEventRecord& event :
         workflow_store_->list_events(request.workflow_id(), static_cast<std::size_t>(request.max_events()))) {
      *response.add_events() = event;
    }
    for (const protocol::WorkflowPlanVersion& plan : workflow_store_->list_plan_versions(
             request.workflow_id(), static_cast<std::size_t>(request.max_plan_versions()))) {
      *response.add_plan_versions() = plan;
    }
    for (const protocol::AuditEntry& entry : workflow_store_->list_audit_entries(
             request.workflow_id(), static_cast<std::size_t>(request.max_audit_entries()))) {
      *response.add_audit_entries() = entry;
    }
    return response;
  }
  response.set_ok(false);
  response.set_error_message("Workflow record not found.");
  return response;
}

protocol::GetPlanDiffResponse ControlPlanService::get_plan_diff(const protocol::GetPlanDiffRequest& request) {
  protocol::GetPlanDiffResponse response;
  const auto plan_versions = workflow_store_->list_plan_versions(request.workflow_id());
  const auto from_it =
      std::ranges::find(plan_versions, request.from_plan_version(), &protocol::WorkflowPlanVersion::version);
  const auto to_it =
      std::ranges::find(plan_versions, request.to_plan_version(), &protocol::WorkflowPlanVersion::version);
  if (from_it == plan_versions.end() || to_it == plan_versions.end()) {
    response.set_ok(false);
    response.set_error_message("Requested plan versions were not found.");
    return response;
  }
  response.set_ok(true);
  *response.mutable_diff() = diff_plan_versions(from_it->response(), to_it->response());
  return response;
}

protocol::WorkflowActionResponse ControlPlanService::pause_workflow(const protocol::PauseWorkflowRequest& request) {
  protocol::WorkflowActionResponse response;
  const auto stored_workflow = workflow_store_->get_workflow(request.workflow_id());
  if (!stored_workflow.has_value()) {
    response.set_ok(false);
    response.set_error_message("Workflow record not found.");
    return response;
  }

  protocol::WorkflowRecord workflow = *stored_workflow;
  workflow.mutable_summary()->set_state(protocol::pb::WORKFLOW_LIFECYCLE_STATE_PAUSED);
  workflow.mutable_summary()->set_updated_at_unix_ms(unix_time_ms_now());
  workflow_store_->upsert_workflow(workflow);

  protocol::AuditEntry audit_entry;
  audit_entry.set_sequence(workflow.summary().total_audit_entry_count() + 1);
  audit_entry.set_recorded_at_unix_ms(unix_time_ms_now());
  audit_entry.set_actor(request.actor().empty() ? kInternalControlActor : request.actor());
  audit_entry.set_action("pause");
  audit_entry.set_detail(request.reason().empty() ? "Workflow paused from the control plane." : request.reason());
  workflow_store_->write_audit_entry(request.workflow_id(), audit_entry);
  workflow.mutable_summary()->set_total_audit_entry_count(audit_entry.sequence());
  workflow_store_->upsert_workflow(workflow);
  publish_workflow_update(request.workflow_id());

  response.set_ok(true);
  *response.mutable_workflow() = workflow;
  return response;
}

protocol::WorkflowActionResponse ControlPlanService::resume_workflow(const protocol::ResumeWorkflowRequest& request) {
  protocol::WorkflowActionResponse response;
  const auto stored_workflow = workflow_store_->get_workflow(request.workflow_id());
  if (!stored_workflow.has_value()) {
    response.set_ok(false);
    response.set_error_message("Workflow record not found.");
    return response;
  }

  protocol::WorkflowRecord workflow = *stored_workflow;
  const protocol::WorkflowLifecycleState resumed_state =
      workflow.has_latest_response() && workflow.latest_response().ok()
          ? protocol::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED
          : protocol::pb::WORKFLOW_LIFECYCLE_STATE_SUBMITTED;
  workflow.mutable_summary()->set_state(resumed_state);
  workflow.mutable_summary()->set_updated_at_unix_ms(unix_time_ms_now());
  workflow_store_->upsert_workflow(workflow);

  protocol::AuditEntry audit_entry;
  audit_entry.set_sequence(workflow.summary().total_audit_entry_count() + 1);
  audit_entry.set_recorded_at_unix_ms(unix_time_ms_now());
  audit_entry.set_actor(request.actor().empty() ? kInternalControlActor : request.actor());
  audit_entry.set_action("resume");
  audit_entry.set_detail(request.reason().empty() ? "Workflow resumed from the control plane." : request.reason());
  workflow_store_->write_audit_entry(request.workflow_id(), audit_entry);
  workflow.mutable_summary()->set_total_audit_entry_count(audit_entry.sequence());
  workflow_store_->upsert_workflow(workflow);
  publish_workflow_update(request.workflow_id());

  response.set_ok(true);
  *response.mutable_workflow() = workflow;
  return response;
}

protocol::WorkflowActionResponse ControlPlanService::cancel_workflow(const protocol::CancelWorkflowRequest& request) {
  protocol::WorkflowActionResponse response;
  const auto stored_workflow = workflow_store_->get_workflow(request.workflow_id());
  if (!stored_workflow.has_value()) {
    response.set_ok(false);
    response.set_error_message("Workflow record not found.");
    return response;
  }

  protocol::WorkflowRecord workflow = *stored_workflow;
  workflow.mutable_summary()->set_state(protocol::pb::WORKFLOW_LIFECYCLE_STATE_CANCELLED);
  workflow.mutable_summary()->set_updated_at_unix_ms(unix_time_ms_now());
  workflow_store_->upsert_workflow(workflow);

  protocol::AuditEntry audit_entry;
  audit_entry.set_sequence(workflow.summary().total_audit_entry_count() + 1);
  audit_entry.set_recorded_at_unix_ms(unix_time_ms_now());
  audit_entry.set_actor(request.actor().empty() ? kInternalControlActor : request.actor());
  audit_entry.set_action("cancel");
  audit_entry.set_detail(request.reason().empty() ? "Workflow cancelled from the control plane." : request.reason());
  workflow_store_->write_audit_entry(request.workflow_id(), audit_entry);
  workflow.mutable_summary()->set_total_audit_entry_count(audit_entry.sequence());
  workflow_store_->upsert_workflow(workflow);
  publish_workflow_update(request.workflow_id());

  response.set_ok(true);
  *response.mutable_workflow() = workflow;
  return response;
}

protocol::WorkflowActionResponse ControlPlanService::apply_manual_intervention(
    const protocol::ManualInterventionRequest& request) {
  protocol::WorkflowActionResponse response;
  const auto stored_workflow = workflow_store_->get_workflow(request.workflow_id());
  if (!stored_workflow.has_value()) {
    response.set_ok(false);
    response.set_error_message("Workflow record not found.");
    return response;
  }

  protocol::WorkflowRecord workflow = *stored_workflow;
  protocol::AuditEntry audit_entry;
  audit_entry.set_sequence(workflow.summary().total_audit_entry_count() + 1);
  audit_entry.set_recorded_at_unix_ms(unix_time_ms_now());
  audit_entry.set_actor(request.actor().empty() ? kInternalControlActor : request.actor());
  audit_entry.set_action("manual_intervention");
  audit_entry.set_detail(request.note().empty() ? "Manual intervention applied." : request.note());
  workflow_store_->write_audit_entry(request.workflow_id(), audit_entry);
  workflow.mutable_summary()->set_total_audit_entry_count(audit_entry.sequence());
  workflow_store_->upsert_workflow(workflow);
  publish_workflow_update(request.workflow_id());

  protocol::ReorchestrateRequest reorchestrate_request;
  reorchestrate_request.set_workflow_id(request.workflow_id());
  *reorchestrate_request.mutable_auth() = request.auth();
  reorchestrate_request.set_trigger_reorchestration(request.trigger_reorchestration());
  if (request.has_idempotency_key()) {
    reorchestrate_request.set_idempotency_key(request.idempotency_key());
  }
  if (request.has_expected_plan_version()) {
    reorchestrate_request.set_expected_plan_version(request.expected_plan_version());
  }
  reorchestrate_request.mutable_task_overrides()->CopyFrom(request.task_overrides());
  reorchestrate_request.mutable_actor_overrides()->CopyFrom(request.actor_overrides());

  const protocol::RuntimeApiResponse runtime_response = reorchestrate(reorchestrate_request);
  if (!runtime_response.ok()) {
    response.set_ok(false);
    response.set_error_message(runtime_response.error_message());
  } else {
    response.set_ok(true);
  }

  if (const auto refreshed_workflow = workflow_store_->get_workflow(request.workflow_id());
      refreshed_workflow.has_value()) {
    *response.mutable_workflow() = *refreshed_workflow;
  }
  return response;
}

void ControlPlanService::publish_workflow_update(const std::string_view workflow_id) const {
  if (!workflow_update_feed_ || workflow_id.empty()) {
    return;
  }
  workflow_update_feed_->publish(std::string(workflow_id));
}

}  // namespace task_orchestrator::control_plane::service
