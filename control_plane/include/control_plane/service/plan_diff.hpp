#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__PLAN_DIFF_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__PLAN_DIFF_HPP_

#include "protocol/control_plane_api.hpp"

namespace task_orchestrator::control_plane::service {

protocol::WorkflowPlanDiff diff_plan_versions(const protocol::RuntimeApiResponse& from,
                                              const protocol::RuntimeApiResponse& to);

}  // namespace task_orchestrator::control_plane::service

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__PLAN_DIFF_HPP_
