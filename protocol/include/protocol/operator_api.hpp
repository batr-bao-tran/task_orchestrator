#ifndef TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__OPERATOR_API_HPP_
#define TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__OPERATOR_API_HPP_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "protocol/runtime_api.hpp"
#include "task_orchestration_service/operator_api.pb.h"

namespace task_orchestrator::protocol {

inline constexpr std::string_view kHttpOperatorDashboardPath = "/v1/operator/dashboard";
inline constexpr std::string_view kHttpOperatorDashboardStreamPath = "/v1/operator/dashboard:stream";
inline constexpr std::string_view kHttpOperatorWorkflowsPath = "/v1/operator/workflows";
inline constexpr std::string_view kHttpOperatorWorkflowsPathPrefix = "/v1/operator/workflows/";
inline constexpr std::string_view kHttpOperatorTasksSuffix = "/tasks";
inline constexpr std::string_view kHttpOperatorDeleteTaskSuffix = ":delete";
inline constexpr std::string_view kHttpOperatorPauseSuffix = ":pause";
inline constexpr std::string_view kHttpOperatorResumeSuffix = ":resume";
inline constexpr std::string_view kHttpOperatorCancelSuffix = ":cancel";
inline constexpr std::string_view kHttpOperatorManualInterventionSuffix = ":manualIntervention";

using OperatorDashboardStats = pb::OperatorDashboardStats;
using OperatorConnectorBinding = pb::OperatorConnectorBinding;
using GetOperatorDashboardRequest = pb::GetOperatorDashboardRequest;
using GetOperatorDashboardResponse = pb::GetOperatorDashboardResponse;
using OperatorDashboardUpdate = pb::OperatorDashboardUpdate;
using UpsertOperatorWorkflowRequest = pb::UpsertOperatorWorkflowRequest;
using UpsertOperatorTaskRequest = pb::UpsertOperatorTaskRequest;
using DeleteOperatorTaskRequest = pb::DeleteOperatorTaskRequest;
using OperatorWorkflowActionRequest = pb::OperatorWorkflowActionRequest;
using ManualInterventionRequest = pb::ManualInterventionRequest;
using OperatorMutationResponse = pb::OperatorMutationResponse;

struct OperatorDashboardNotification {
  std::uint64_t event_id = 0;
  std::string workflow_id;
};

class WorkflowOperatorService {
 public:
  virtual ~WorkflowOperatorService() noexcept = default;

  virtual GetOperatorDashboardResponse get_dashboard(const GetOperatorDashboardRequest& request) = 0;
  virtual OperatorDashboardUpdate get_dashboard_update(const GetOperatorDashboardRequest& request,
                                                       const OperatorDashboardNotification& notification) = 0;
  virtual OperatorMutationResponse upsert_workflow(const UpsertOperatorWorkflowRequest& request) = 0;
  virtual OperatorMutationResponse upsert_task(const UpsertOperatorTaskRequest& request) = 0;
  virtual OperatorMutationResponse delete_task(const DeleteOperatorTaskRequest& request) = 0;
  virtual OperatorMutationResponse pause_workflow(const OperatorWorkflowActionRequest& request) = 0;
  virtual OperatorMutationResponse resume_workflow(const OperatorWorkflowActionRequest& request) = 0;
  virtual OperatorMutationResponse cancel_workflow(const OperatorWorkflowActionRequest& request) = 0;
  virtual OperatorMutationResponse apply_manual_intervention(const ManualInterventionRequest& request) = 0;
};

class WorkflowOperatorEventService {
 public:
  virtual ~WorkflowOperatorEventService() noexcept = default;

  virtual std::uint64_t latest_dashboard_event_id() const = 0;
  virtual std::optional<OperatorDashboardNotification> wait_for_dashboard_update(std::uint64_t after_event_id,
                                                                                 std::chrono::milliseconds timeout) = 0;
};

}  // namespace task_orchestrator::protocol

#endif  // TASK_ORCHESTRATOR__PROTOCOL_INCLUDE_PROTOCOL__OPERATOR_API_HPP_
