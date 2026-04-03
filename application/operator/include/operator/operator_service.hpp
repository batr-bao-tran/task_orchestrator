#ifndef TASK_ORCHESTRATOR__APPLICATION_OPERATOR_INCLUDE_OPERATOR__OPERATOR_SERVICE_HPP_
#define TASK_ORCHESTRATOR__APPLICATION_OPERATOR_INCLUDE_OPERATOR__OPERATOR_SERVICE_HPP_

#include <memory>
#include <string>
#include <string_view>

#include "control_plane/integration/connector_registry.hpp"
#include "control_plane/service/workflow_update_feed.hpp"
#include "control_plane/store/workflow_store.hpp"
#include "google/protobuf/repeated_ptr_field.h"
#include "protocol/control_plane_api.hpp"
#include "protocol/operator_api.hpp"
#include "protocol/runtime_api.hpp"

namespace task_orchestrator::app::operator_api {

class OperatorService final : public protocol::WorkflowOperatorService, public protocol::WorkflowOperatorEventService {
 public:
  OperatorService(protocol::WorkflowRuntimeService& runtime_service,
                  protocol::WorkflowControlPlaneService& control_plane_service,
                  std::shared_ptr<control_plane::store::WorkflowStore> workflow_store,
                  std::shared_ptr<control_plane::integration::ConnectorRegistry> connector_registry,
                  std::shared_ptr<control_plane::service::WorkflowUpdateFeed> workflow_update_feed);
  ~OperatorService() noexcept override = default;

  OperatorService(const OperatorService&) = delete;
  OperatorService& operator=(const OperatorService&) = delete;

  protocol::GetOperatorDashboardResponse get_dashboard(const protocol::GetOperatorDashboardRequest& request) override;
  protocol::OperatorDashboardUpdate get_dashboard_update(
      const protocol::GetOperatorDashboardRequest& request,
      const protocol::OperatorDashboardNotification& notification) override;
  protocol::OperatorMutationResponse upsert_workflow(const protocol::UpsertOperatorWorkflowRequest& request) override;
  protocol::OperatorMutationResponse upsert_task(const protocol::UpsertOperatorTaskRequest& request) override;
  protocol::OperatorMutationResponse delete_task(const protocol::DeleteOperatorTaskRequest& request) override;
  protocol::OperatorMutationResponse pause_workflow(const protocol::OperatorWorkflowActionRequest& request) override;
  protocol::OperatorMutationResponse resume_workflow(const protocol::OperatorWorkflowActionRequest& request) override;
  protocol::OperatorMutationResponse cancel_workflow(const protocol::OperatorWorkflowActionRequest& request) override;
  protocol::OperatorMutationResponse apply_manual_intervention(
      const protocol::ManualInterventionRequest& request) override;
  std::uint64_t latest_dashboard_event_id() const override;
  std::optional<protocol::OperatorDashboardNotification> wait_for_dashboard_update(
      std::uint64_t after_event_id, std::chrono::milliseconds timeout) override;

 private:
  void populate_dashboard_stats(protocol::OperatorDashboardStats* stats, std::int64_t now) const;
  void populate_connector_bindings(
      google::protobuf::RepeatedPtrField<protocol::OperatorConnectorBinding>* bindings) const;
  void populate_workflow_summaries(google::protobuf::RepeatedPtrField<protocol::WorkflowSummary>* workflows,
                                   std::string_view workflow_query,
                                   std::int32_t workflow_page_size) const;
  static std::string resolve_selected_workflow_id(
      std::string selected_workflow_id, const google::protobuf::RepeatedPtrField<protocol::WorkflowSummary>& workflows);
  bool populate_selected_workflow(std::string_view workflow_id,
                                  std::int32_t max_events,
                                  std::int32_t max_plan_versions,
                                  std::int32_t max_audit_entries,
                                  protocol::GetWorkflowHistoryResponse* history_response,
                                  protocol::GetPlanDiffResponse* plan_diff_response,
                                  std::string* error_message) const;
  protocol::GetOperatorDashboardResponse build_dashboard(std::string selected_workflow_id,
                                                         std::string_view workflow_query,
                                                         const protocol::pb::ClientAuthContext& auth_context,
                                                         std::int32_t workflow_page_size,
                                                         std::int32_t max_events,
                                                         std::int32_t max_plan_versions,
                                                         std::int32_t max_audit_entries);
  protocol::OperatorDashboardUpdate build_dashboard_update(const protocol::GetOperatorDashboardRequest& request,
                                                           const protocol::OperatorDashboardNotification& notification);
  protocol::OperatorMutationResponse build_mutation_response(std::string selected_workflow_id,
                                                             bool ok,
                                                             std::string error_message);
  void append_audit_entry(std::string_view workflow_id, std::string actor, std::string action, std::string detail);
  void publish_dashboard_update(std::string_view workflow_id) const;

  protocol::WorkflowRuntimeService& runtime_service_;
  protocol::WorkflowControlPlaneService& control_plane_service_;
  std::shared_ptr<control_plane::store::WorkflowStore> workflow_store_;
  std::shared_ptr<control_plane::integration::ConnectorRegistry> connector_registry_;
  std::shared_ptr<control_plane::service::WorkflowUpdateFeed> workflow_update_feed_;
};

}  // namespace task_orchestrator::app::operator_api

#endif  // TASK_ORCHESTRATOR__APPLICATION_OPERATOR_INCLUDE_OPERATOR__OPERATOR_SERVICE_HPP_
