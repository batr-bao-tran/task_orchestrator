#include "runtime_service/in_memory_runtime_service.hpp"

#include <algorithm>
#include <future>
#include <optional>
#include <ranges>
#include <utility>

#include "runner/runner.hpp"
#include "runtime_service/src/in_memory_runtime_service_detail.hpp"
#include "utils/logger.hpp"
#include "utils/task_executor.hpp"

namespace task_orchestrator::app {
namespace {

namespace pb = ::task_orchestrator::protocol::pb;
using detail::consume_final_response;
using detail::kRuntimeOverrideAvailabilityEnd;
using detail::kRuntimeOverrideAvailabilityStart;
using detail::make_event;
using detail::make_response_event;
using detail::make_runtime_error_response;
using detail::make_runtime_response;
using detail::remove_completed_task_dependencies;
using detail::task_duration_for_id;
using detail::to_app_workflow;

}  // namespace

InMemoryWorkflowRuntimeService::InMemoryWorkflowRuntimeService(protocol::SecurityConfig security,
                                                               task_orchestrator::TaskExecutor* task_executor)
    : security_(std::move(security)),
      task_executor_(task_executor != nullptr ? task_executor : &get_shared_task_executor()) {}

protocol::RuntimeApiResponse InMemoryWorkflowRuntimeService::authorize(const pb::ClientAuthContext& auth) const {
  if (security_.require_secure_transport && !auth.secure_transport()) {
    return make_runtime_error_response("Secure transport is required for this API.");
  }

  if (security_.mode == protocol::AuthMode::BearerToken && auth.bearer_token() != security_.expected_credential) {
    return make_runtime_error_response("Bearer token authentication failed.");
  }
  if (security_.mode == protocol::AuthMode::ApiKey && auth.api_key() != security_.expected_credential) {
    return make_runtime_error_response("API key authentication failed.");
  }

  protocol::RuntimeApiResponse response;
  response.set_ok(true);
  response.mutable_result()->set_ok(true);
  return response;
}

protocol::RuntimeApiResponse InMemoryWorkflowRuntimeService::submit_workflow(
    const protocol::SubmitWorkflowRequest& request) {
  return consume_final_response(stream_submit_workflow(request));
}

protocol::RuntimeApiResponse InMemoryWorkflowRuntimeService::reorchestrate(
    const protocol::ReorchestrateRequest& request) {
  return consume_final_response(stream_reorchestrate(request));
}

std::future<protocol::RuntimeApiResponse> InMemoryWorkflowRuntimeService::submit_workflow_async(
    const protocol::SubmitWorkflowRequest& request) {
  return task_executor_->try_submit([this, request]() { return submit_workflow(request); });
}

std::future<protocol::RuntimeApiResponse> InMemoryWorkflowRuntimeService::reorchestrate_async(
    const protocol::ReorchestrateRequest& request) {
  return task_executor_->try_submit([this, request]() { return reorchestrate(request); });
}

protocol::WorkflowEventStream InMemoryWorkflowRuntimeService::stream_submit_workflow(
    protocol::SubmitWorkflowRequest request) {
  auto logger = get_logger(LogLayer::Application);
  const WorkflowId workflow_id = request.config().id();
  if (protocol::RuntimeApiResponse auth_response = authorize(request.auth()); !auth_response.ok()) {
    logger->warn("Workflow submission rejected: {}", auth_response.error_message());
    co_yield make_response_event(
        pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, workflow_id, auth_response, auth_response.error_message());
    co_return;
  }

  WorkflowConfig workflow_config = to_app_workflow(request.config());
  const bool replace_existing = request.has_replace_existing() ? request.replace_existing() : true;
  std::optional<protocol::RuntimeApiResponse> duplicate_workflow_response;
  {
    std::scoped_lock lock(mutex_);
    if (!replace_existing && workflows_.contains(workflow_config.id)) {
      duplicate_workflow_response =
          make_runtime_error_response("Workflow already exists and replace_existing is false.");
    } else {
      workflows_[workflow_config.id] = workflow_config;
    }
  }

  if (duplicate_workflow_response.has_value()) {
    co_yield make_response_event(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED,
                                 workflow_config.id,
                                 *duplicate_workflow_response,
                                 duplicate_workflow_response->error_message());
    co_return;
  }

  co_yield make_event(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, workflow_config.id, "Workflow accepted and stored.");
  co_yield make_event(pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED, workflow_config.id, "Initial optimization started.");

  const RunResult result = optimize(workflow_config);
  const protocol::RuntimeApiResponse response = make_runtime_response(result, workflow_config);
  for (const Assignment& assignment : result.assignments) {
    protocol::WorkflowEvent event =
        make_event(pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED, workflow_config.id, "Task assignment planned.");
    event.set_task_id(assignment.task_id);
    event.set_actor_id(assignment.actor_id);
    event.set_start_time(assignment.start_time);
    event.set_end_time(assignment.start_time + task_duration_for_id(workflow_config, assignment.task_id));
    co_yield event;
  }

  logger->info("Submitted workflow '{}' through runtime API.", workflow_config.id);
  co_yield make_response_event(
      pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, workflow_config.id, response, "Initial optimization finished.");
}

protocol::WorkflowEventStream InMemoryWorkflowRuntimeService::stream_reorchestrate(
    protocol::ReorchestrateRequest request) {
  auto logger = get_logger(LogLayer::Application);
  if (protocol::RuntimeApiResponse auth_response = authorize(request.auth()); !auth_response.ok()) {
    logger->warn("Re-orchestration rejected: {}", auth_response.error_message());
    co_yield make_response_event(
        pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED, request.workflow_id(), auth_response, auth_response.error_message());
    co_return;
  }

  WorkflowConfig workflow_config;
  std::optional<protocol::RuntimeApiResponse> missing_workflow_response;
  {
    std::scoped_lock lock(mutex_);
    const auto workflow_it = workflows_.find(request.workflow_id());
    if (workflow_it == workflows_.end()) {
      missing_workflow_response = make_runtime_error_response("Workflow session not found.");
    } else {
      workflow_config = workflow_it->second;
      for (const pb::TaskStateOverride& task_override : request.task_overrides()) {
        apply_task_override(workflow_config, task_override);
      }
      for (const pb::ActorStateOverride& actor_override : request.actor_overrides()) {
        apply_actor_override(workflow_config, actor_override);
      }
      workflow_it->second = workflow_config;
    }
  }

  if (missing_workflow_response.has_value()) {
    co_yield make_response_event(pb::WORKFLOW_EVENT_TYPE_REQUEST_REJECTED,
                                 request.workflow_id(),
                                 *missing_workflow_response,
                                 missing_workflow_response->error_message());
    co_return;
  }

  for (const pb::TaskStateOverride& task_override : request.task_overrides()) {
    if (task_override.has_completed() && task_override.completed()) {
      protocol::WorkflowEvent event =
          make_event(pb::WORKFLOW_EVENT_TYPE_TASK_COMPLETED, request.workflow_id(), "Task marked completed.");
      event.set_task_id(task_override.task_id());
      co_yield event;
      continue;
    }
    protocol::WorkflowEvent event =
        make_event(pb::WORKFLOW_EVENT_TYPE_RUNTIME_OVERRIDE_APPLIED, request.workflow_id(), "Task override applied.");
    event.set_task_id(task_override.task_id());
    co_yield event;
  }
  for (const pb::ActorStateOverride& actor_override : request.actor_overrides()) {
    protocol::WorkflowEvent event =
        make_event(pb::WORKFLOW_EVENT_TYPE_RUNTIME_OVERRIDE_APPLIED, request.workflow_id(), "Actor override applied.");
    event.set_actor_id(actor_override.actor_id());
    co_yield event;
  }

  const bool trigger_reorchestration = request.has_trigger_reorchestration() ? request.trigger_reorchestration() : true;
  if (!trigger_reorchestration) {
    protocol::RuntimeApiResponse response;
    response.set_ok(true);
    response.mutable_result()->set_ok(true);
    logger->info("Stored runtime overrides for workflow '{}' without replanning.", request.workflow_id());
    co_yield make_response_event(
        pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, request.workflow_id(), response, "Overrides stored without replanning.");
    co_return;
  }

  co_yield make_event(pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED, request.workflow_id(), "Replanning started.");
  const RunResult result = optimize(workflow_config);
  const protocol::RuntimeApiResponse response = make_runtime_response(result, workflow_config);
  for (const Assignment& assignment : result.assignments) {
    protocol::WorkflowEvent event =
        make_event(pb::WORKFLOW_EVENT_TYPE_TASK_PLANNED, request.workflow_id(), "Task assignment planned.");
    event.set_task_id(assignment.task_id);
    event.set_actor_id(assignment.actor_id);
    event.set_start_time(assignment.start_time);
    event.set_end_time(assignment.start_time + task_duration_for_id(workflow_config, assignment.task_id));
    co_yield event;
  }

  logger->info("Re-orchestrated workflow '{}' through runtime API.", request.workflow_id());
  co_yield make_response_event(
      pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, request.workflow_id(), response, "Replanning finished.");
}

void InMemoryWorkflowRuntimeService::apply_task_override(WorkflowConfig& config,
                                                         const pb::TaskStateOverride& override_request) {
  auto task_it = std::ranges::find(config.tasks, override_request.task_id(), &TaskConfig::id);
  if (task_it == config.tasks.end()) {
    return;
  }

  if (override_request.has_completed() && override_request.completed()) {
    config.tasks.erase(task_it);
    remove_completed_task_dependencies(config, override_request.task_id());
    return;
  }

  if (override_request.has_requested_time()) {
    task_it->requested_time = override_request.requested_time();
  }
  if (override_request.has_deadline()) {
    task_it->deadline = override_request.deadline();
  }
  if (override_request.has_priority()) {
    task_it->priority = override_request.priority();
  }
  if (override_request.has_pinned_actor_id()) {
    task_it->allowed_actor_ids = {override_request.pinned_actor_id()};
  }
}

void InMemoryWorkflowRuntimeService::apply_actor_override(WorkflowConfig& config,
                                                          const pb::ActorStateOverride& override_request) {
  auto actor_it = std::ranges::find(config.actors, override_request.actor_id(), &ActorConfig::id);
  if (actor_it == config.actors.end()) {
    return;
  }

  if (override_request.has_capacity()) {
    actor_it->capacity = override_request.capacity();
  }
  if (override_request.has_unavailable()) {
    if (override_request.unavailable()) {
      actor_it->windows.clear();
    } else if (actor_it->windows.empty()) {
      actor_it->windows.push_back(
          AvailabilityWindowConfig{.start = kRuntimeOverrideAvailabilityStart, .end = kRuntimeOverrideAvailabilityEnd});
    }
  }
}

}  // namespace task_orchestrator::app
