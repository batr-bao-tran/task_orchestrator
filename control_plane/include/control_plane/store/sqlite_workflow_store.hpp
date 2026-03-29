#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_STORE__SQLITE_WORKFLOW_STORE_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_STORE__SQLITE_WORKFLOW_STORE_HPP_

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "control_plane/store/workflow_store.hpp"

namespace SQLite {
class Database;
}

namespace task_orchestrator::control_plane::store {

class SqliteWorkflowStore final : public WorkflowStore {
 public:
  explicit SqliteWorkflowStore(std::filesystem::path database_path, std::string current_boot_id = {});
  ~SqliteWorkflowStore() noexcept override;

  void upsert_workflow(const protocol::WorkflowRecord& workflow) override;
  std::optional<protocol::WorkflowRecord> get_workflow(std::string_view workflow_id) const override;
  std::vector<protocol::WorkflowRecord> list_all_workflows() const override;

  protocol::ListWorkflowsResponse list_workflows(const protocol::ListWorkflowsRequest& request) const override;
  protocol::SearchWorkflowsResponse search_workflows(const protocol::SearchWorkflowsRequest& request) const override;

  void write_event(std::string_view workflow_id, const protocol::WorkflowEventRecord& event_record) override;
  std::vector<protocol::WorkflowEventRecord> list_events(std::string_view workflow_id,
                                                         std::size_t limit = 0) const override;

  void write_plan_version(std::string_view workflow_id, const protocol::WorkflowPlanVersion& plan_version) override;
  std::vector<protocol::WorkflowPlanVersion> list_plan_versions(std::string_view workflow_id,
                                                                std::size_t limit = 0) const override;

  void write_audit_entry(std::string_view workflow_id, const protocol::AuditEntry& audit_entry) override;
  std::vector<protocol::AuditEntry> list_audit_entries(std::string_view workflow_id,
                                                       std::size_t limit = 0) const override;

  void put_idempotency_record(const protocol::IdempotencyRecord& record) override;
  std::optional<protocol::IdempotencyRecord> get_idempotency_record(std::string_view key) const override;

  StorageMetrics get_storage_metrics(std::int64_t recent_since_unix_ms) const override;
  StoragePruneResult prune_stale_boot_data(std::int64_t recorded_before_unix_ms) override;

  [[nodiscard]] const std::filesystem::path& database_path() const noexcept { return database_path_; }
  [[nodiscard]] const std::string& current_boot_id() const noexcept { return current_boot_id_; }

 private:
  void configure_database();
  void initialize_schema();

  std::filesystem::path database_path_;
  std::string current_boot_id_;
  std::unique_ptr<SQLite::Database> database_;
  mutable std::mutex mutex_;
};

}  // namespace task_orchestrator::control_plane::store

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_STORE__SQLITE_WORKFLOW_STORE_HPP_
