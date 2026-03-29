#ifndef TASK_ORCHESTRATOR__APPLICATION_RUNTIME_SERVICE_INCLUDE_RUNTIME_SERVICE__IN_MEMORY_RUNTIME_SERVICE_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_RUNTIME_SERVICE_INCLUDE_RUNTIME_SERVICE__IN_MEMORY_RUNTIME_SERVICE_HPP_

#include <future>
#include <mutex>
#include <unordered_map>

#include "config/config.hpp"
#include "protocol/runtime_api.hpp"

namespace task_orchestrator {
class TaskExecutor;
}

namespace task_orchestrator::app {

/** @brief In-memory workflow runtime service used by the transport adapters. */
class InMemoryWorkflowRuntimeService final : public protocol::WorkflowRuntimeService {
 public:
  /** @brief Construct a runtime service with security policy and optional executor override. */
  explicit InMemoryWorkflowRuntimeService(protocol::SecurityConfig security = {},
                                          task_orchestrator::TaskExecutor* task_executor = nullptr);

  /** @brief Submit a workflow and return the final orchestration response. */
  protocol::RuntimeApiResponse submit_workflow(const protocol::SubmitWorkflowRequest& request) override;
  /** @brief Re-run planning for an existing in-memory workflow session. */
  protocol::RuntimeApiResponse reorchestrate(const protocol::ReorchestrateRequest& request) override;

  /** @brief Submit a workflow on the asynchronous executor-backed path. */
  std::future<protocol::RuntimeApiResponse> submit_workflow_async(
      const protocol::SubmitWorkflowRequest& request) override;
  /** @brief Re-run planning asynchronously for an existing workflow session. */
  std::future<protocol::RuntimeApiResponse> reorchestrate_async(const protocol::ReorchestrateRequest& request) override;
  /** @brief Stream workflow submission progress and final response events. */
  protocol::WorkflowEventStream stream_submit_workflow(protocol::SubmitWorkflowRequest request) override;
  /** @brief Stream re-orchestration progress and final response events. */
  protocol::WorkflowEventStream stream_reorchestrate(protocol::ReorchestrateRequest request) override;

 private:
  protocol::RuntimeApiResponse authorize(const task_orchestrator::protocol::pb::ClientAuthContext& auth) const;
  static void apply_task_override(WorkflowConfig& config,
                                  const task_orchestrator::protocol::pb::TaskStateOverride& override_request);
  static void apply_actor_override(WorkflowConfig& config,
                                   const task_orchestrator::protocol::pb::ActorStateOverride& override_request);

  protocol::SecurityConfig security_;
  task_orchestrator::TaskExecutor* task_executor_;
  mutable std::mutex mutex_;
  std::unordered_map<WorkflowId, WorkflowConfig> workflows_;
};

}  // namespace task_orchestrator::app

#endif  // TASK_ORCHESTRATOR__APPLICATION_RUNTIME_SERVICE_INCLUDE_RUNTIME_SERVICE__IN_MEMORY_RUNTIME_SERVICE_HPP_
