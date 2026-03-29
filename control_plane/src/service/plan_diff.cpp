#include "control_plane/service/plan_diff.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace task_orchestrator::control_plane::service {
namespace {

using AssignmentMap = std::map<std::string, protocol::pb::Assignment>;

AssignmentMap index_assignments(const protocol::RuntimeApiResponse& response) {
  AssignmentMap assignments;
  if (!response.ok() || !response.result().ok()) {
    return assignments;
  }
  for (const auto& assignment : response.result().assignments()) {
    assignments.emplace(assignment.task_id(), assignment);
  }
  return assignments;
}

bool assignment_changed(const protocol::pb::Assignment& before, const protocol::pb::Assignment& after) {
  return before.actor_id() != after.actor_id() || before.start_time() != after.start_time() ||
         before.end_time() != after.end_time();
}

}  // namespace

protocol::WorkflowPlanDiff diff_plan_versions(const protocol::RuntimeApiResponse& from,
                                              const protocol::RuntimeApiResponse& to) {
  protocol::WorkflowPlanDiff diff;
  const AssignmentMap from_assignments = index_assignments(from);
  const AssignmentMap to_assignments = index_assignments(to);

  for (const auto& [task_id, to_assignment] : to_assignments) {
    const auto from_it = from_assignments.find(task_id);
    if (from_it == from_assignments.end()) {
      *diff.add_added_assignments() = to_assignment;
      continue;
    }
    if (assignment_changed(from_it->second, to_assignment)) {
      protocol::AssignmentDiff* changed_assignment = diff.add_changed_assignments();
      *changed_assignment->mutable_before() = from_it->second;
      *changed_assignment->mutable_after() = to_assignment;
    }
  }

  for (const auto& [task_id, from_assignment] : from_assignments) {
    if (!to_assignments.contains(task_id)) {
      *diff.add_removed_assignments() = from_assignment;
    }
  }

  return diff;
}

}  // namespace task_orchestrator::control_plane::service
