#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_STORE__WORKFLOW_STORE_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_STORE__WORKFLOW_STORE_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "protocol/control_plane_api.hpp"

namespace task_orchestrator::control_plane::store {

struct StoragePruneResult {
  std::int64_t pruned_event_rows = 0;
  std::int64_t pruned_plan_version_rows = 0;
  std::int64_t pruned_audit_entry_rows = 0;
  std::int64_t pruned_idempotency_rows = 0;
};

struct StorageMetrics {
  std::int64_t recent_event_count = 0;
  std::int64_t retained_plan_versions = 0;
  std::int64_t tracked_workflows = 0;
  std::int64_t active_workflows = 0;
};

class WorkflowStore {
 public:
  virtual ~WorkflowStore() noexcept = default;

  virtual void upsert_workflow(const protocol::WorkflowRecord& workflow) = 0;
  virtual std::optional<protocol::WorkflowRecord> get_workflow(std::string_view workflow_id) const = 0;
  virtual std::vector<protocol::WorkflowRecord> list_all_workflows() const = 0;

  virtual protocol::ListWorkflowsResponse list_workflows(const protocol::ListWorkflowsRequest& request) const = 0;
  virtual protocol::SearchWorkflowsResponse search_workflows(const protocol::SearchWorkflowsRequest& request) const = 0;

  virtual void write_event(std::string_view workflow_id, const protocol::WorkflowEventRecord& event_record) = 0;
  virtual std::vector<protocol::WorkflowEventRecord> list_events(std::string_view workflow_id,
                                                                 std::size_t limit = 0) const = 0;

  virtual void write_plan_version(std::string_view workflow_id, const protocol::WorkflowPlanVersion& plan_version) = 0;
  virtual std::vector<protocol::WorkflowPlanVersion> list_plan_versions(std::string_view workflow_id,
                                                                        std::size_t limit = 0) const = 0;

  virtual void write_audit_entry(std::string_view workflow_id, const protocol::AuditEntry& audit_entry) = 0;
  virtual std::vector<protocol::AuditEntry> list_audit_entries(std::string_view workflow_id,
                                                               std::size_t limit = 0) const = 0;

  virtual void put_idempotency_record(const protocol::IdempotencyRecord& record) = 0;
  virtual std::optional<protocol::IdempotencyRecord> get_idempotency_record(std::string_view key) const = 0;

  virtual StorageMetrics get_storage_metrics(std::int64_t recent_since_unix_ms) const = 0;
  virtual StoragePruneResult prune_stale_boot_data(std::int64_t recorded_before_unix_ms) = 0;
};

}  // namespace task_orchestrator::control_plane::store

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_STORE__WORKFLOW_STORE_HPP_
