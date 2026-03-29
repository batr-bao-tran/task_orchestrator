#ifndef TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__CONTROL_PLANE_API_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__CONTROL_PLANE_API_HPP_

#include <string_view>

#include "protocol/runtime_api.hpp"
#include "task_orchestration_service/control_plane.pb.h"

namespace task_orchestrator::protocol {

inline constexpr std::string_view kHttpControlPlaneWorkflowsPath = "/v1/control-plane/workflows";
inline constexpr std::string_view kHttpControlPlaneSearchPath = "/v1/control-plane/workflows:search";
inline constexpr std::string_view kHttpControlPlaneHistorySuffix = "/history";
inline constexpr std::string_view kHttpControlPlanePauseSuffix = ":pause";
inline constexpr std::string_view kHttpControlPlaneResumeSuffix = ":resume";
inline constexpr std::string_view kHttpControlPlaneCancelSuffix = ":cancel";
inline constexpr std::string_view kHttpControlPlanePlanDiffSuffix = ":planDiff";
inline constexpr std::string_view kHttpControlPlaneManualInterventionSuffix = ":manualIntervention";

using WorkflowLifecycleState = pb::WorkflowLifecycleState;
using WorkflowSummary = pb::WorkflowSummary;
using WorkflowRecord = pb::WorkflowRecord;
using WorkflowEventRecord = pb::WorkflowEventRecord;
using WorkflowPlanVersion = pb::WorkflowPlanVersion;
using AuditEntry = pb::AuditEntry;
using IdempotencyRecord = pb::IdempotencyRecord;
using ListWorkflowsRequest = pb::ListWorkflowsRequest;
using ListWorkflowsResponse = pb::ListWorkflowsResponse;
using SearchWorkflowsRequest = pb::SearchWorkflowsRequest;
using SearchWorkflowsResponse = pb::SearchWorkflowsResponse;
using GetWorkflowRequest = pb::GetWorkflowRequest;
using GetWorkflowResponse = pb::GetWorkflowResponse;
using GetWorkflowHistoryRequest = pb::GetWorkflowHistoryRequest;
using GetWorkflowHistoryResponse = pb::GetWorkflowHistoryResponse;
using PauseWorkflowRequest = pb::PauseWorkflowRequest;
using ResumeWorkflowRequest = pb::ResumeWorkflowRequest;
using CancelWorkflowRequest = pb::CancelWorkflowRequest;
using WorkflowActionResponse = pb::WorkflowActionResponse;
using ManualInterventionRequest = pb::ManualInterventionRequest;
using AssignmentDiff = pb::AssignmentDiff;
using WorkflowPlanDiff = pb::WorkflowPlanDiff;
using GetPlanDiffRequest = pb::GetPlanDiffRequest;
using GetPlanDiffResponse = pb::GetPlanDiffResponse;

class WorkflowControlPlaneService {
 public:
  virtual ~WorkflowControlPlaneService() noexcept = default;

  virtual ListWorkflowsResponse list_workflows(const ListWorkflowsRequest& request) = 0;
  virtual SearchWorkflowsResponse search_workflows(const SearchWorkflowsRequest& request) = 0;
  virtual GetWorkflowResponse get_workflow(const GetWorkflowRequest& request) = 0;
  virtual GetWorkflowHistoryResponse get_workflow_history(const GetWorkflowHistoryRequest& request) = 0;
  virtual GetPlanDiffResponse get_plan_diff(const GetPlanDiffRequest& request) = 0;
  virtual WorkflowActionResponse pause_workflow(const PauseWorkflowRequest& request) = 0;
  virtual WorkflowActionResponse resume_workflow(const ResumeWorkflowRequest& request) = 0;
  virtual WorkflowActionResponse cancel_workflow(const CancelWorkflowRequest& request) = 0;
  virtual WorkflowActionResponse apply_manual_intervention(const ManualInterventionRequest& request) = 0;
};

}  // namespace task_orchestrator::protocol

#endif  // TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__CONTROL_PLANE_API_HPP_
