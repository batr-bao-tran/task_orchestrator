#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__CONTROL_PLAN_SERVICE_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__CONTROL_PLAN_SERVICE_HPP_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "control_plane/integration/connector_registry.hpp"
#include "control_plane/service/workflow_update_feed.hpp"
#include "control_plane/store/workflow_store.hpp"
#include "protocol/control_plane_api.hpp"
#include "protocol/runtime_api.hpp"

namespace task_orchestrator::control_plane::service {

class ControlPlanService final : public protocol::WorkflowRuntimeService, public protocol::WorkflowControlPlaneService {
 public:
  ControlPlanService(std::unique_ptr<protocol::WorkflowRuntimeService> runtime_service,
                     std::shared_ptr<store::WorkflowStore> workflow_store,
                     std::shared_ptr<WorkflowUpdateFeed> workflow_update_feed = make_in_memory_workflow_update_feed(),
                     std::shared_ptr<integration::ConnectorRegistry> connector_registry =
                         integration::make_in_memory_connector_registry());
  ~ControlPlanService() noexcept override = default;

  ControlPlanService(const ControlPlanService&) = delete;
  ControlPlanService& operator=(const ControlPlanService&) = delete;

  void recover_active_workflows(const protocol::pb::ClientAuthContext& auth_context);

  protocol::RuntimeApiResponse submit_workflow(const protocol::SubmitWorkflowRequest& request) override;
  protocol::RuntimeApiResponse reorchestrate(const protocol::ReorchestrateRequest& request) override;
  std::future<protocol::RuntimeApiResponse> submit_workflow_async(
      const protocol::SubmitWorkflowRequest& request) override;
  std::future<protocol::RuntimeApiResponse> reorchestrate_async(const protocol::ReorchestrateRequest& request) override;
  protocol::WorkflowEventStream stream_submit_workflow(protocol::SubmitWorkflowRequest request) override;
  protocol::WorkflowEventStream stream_reorchestrate(protocol::ReorchestrateRequest request) override;

  protocol::ListWorkflowsResponse list_workflows(const protocol::ListWorkflowsRequest& request) override;
  protocol::SearchWorkflowsResponse search_workflows(const protocol::SearchWorkflowsRequest& request) override;
  protocol::GetWorkflowResponse get_workflow(const protocol::GetWorkflowRequest& request) override;
  protocol::GetWorkflowHistoryResponse get_workflow_history(
      const protocol::GetWorkflowHistoryRequest& request) override;
  protocol::GetPlanDiffResponse get_plan_diff(const protocol::GetPlanDiffRequest& request) override;
  protocol::WorkflowActionResponse pause_workflow(const protocol::PauseWorkflowRequest& request) override;
  protocol::WorkflowActionResponse resume_workflow(const protocol::ResumeWorkflowRequest& request) override;
  protocol::WorkflowActionResponse cancel_workflow(const protocol::CancelWorkflowRequest& request) override;
  protocol::WorkflowActionResponse apply_manual_intervention(
      const protocol::ManualInterventionRequest& request) override;

 private:
  void publish_workflow_update(std::string_view workflow_id) const;

  std::unique_ptr<protocol::WorkflowRuntimeService> runtime_service_;
  std::shared_ptr<store::WorkflowStore> workflow_store_;
  std::shared_ptr<WorkflowUpdateFeed> workflow_update_feed_;
  std::shared_ptr<integration::ConnectorRegistry> connector_registry_;
  mutable std::mutex hydrated_mutex_;
  std::unordered_set<std::string> hydrated_workflows_;
};

}  // namespace task_orchestrator::control_plane::service

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__CONTROL_PLAN_SERVICE_HPP_
