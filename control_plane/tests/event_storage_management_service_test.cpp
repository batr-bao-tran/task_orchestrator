#include "control_plane/service/event_storage_management_service.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace {
namespace tcs = task_orchestrator::control_plane::service;
namespace tcp = task_orchestrator::control_plane;
namespace tp = task_orchestrator::protocol;

class FakeWorkflowStore final : public tcp::store::WorkflowStore {
 public:
  void upsert_workflow(const tp::WorkflowRecord&) override {}
  std::optional<tp::WorkflowRecord> get_workflow(std::string_view) const override { return std::nullopt; }
  std::vector<tp::WorkflowRecord> list_all_workflows() const override { return {}; }
  tp::ListWorkflowsResponse list_workflows(const tp::ListWorkflowsRequest&) const override { return {}; }
  tp::SearchWorkflowsResponse search_workflows(const tp::SearchWorkflowsRequest&) const override { return {}; }
  void write_event(std::string_view, const tp::WorkflowEventRecord&) override {}
  std::vector<tp::WorkflowEventRecord> list_events(std::string_view, std::size_t) const override { return {}; }
  void write_plan_version(std::string_view, const tp::WorkflowPlanVersion&) override {}
  std::vector<tp::WorkflowPlanVersion> list_plan_versions(std::string_view, std::size_t) const override { return {}; }
  void write_audit_entry(std::string_view, const tp::AuditEntry&) override {}
  std::vector<tp::AuditEntry> list_audit_entries(std::string_view, std::size_t) const override { return {}; }
  void put_idempotency_record(const tp::IdempotencyRecord&) override {}
  std::optional<tp::IdempotencyRecord> get_idempotency_record(std::string_view) const override { return std::nullopt; }
  tcp::store::StorageMetrics get_storage_metrics(std::int64_t) const override { return {}; }

  tcp::store::StoragePruneResult prune_stale_boot_data(const std::int64_t recorded_before_unix_ms) override {
    ++prune_call_count;
    last_cutoff_unix_ms = recorded_before_unix_ms;
    return prune_result;
  }

  int prune_call_count = 0;
  std::int64_t last_cutoff_unix_ms = 0;
  tcp::store::StoragePruneResult prune_result{
      .pruned_event_rows = 2,
      .pruned_plan_version_rows = 1,
      .pruned_audit_entry_rows = 3,
      .pruned_idempotency_rows = 4,
  };
};

TEST(EventStorageManagementServiceTest, SkipsPruningWhenDisabled) {
  auto store = std::make_shared<FakeWorkflowStore>();
  tcs::EventStorageManagementService service(store, 0);

  const auto outcome = service.prune_stale_boot_history();
  EXPECT_FALSE(outcome.performed);
  EXPECT_EQ(0, store->prune_call_count);
}

TEST(EventStorageManagementServiceTest, PrunesUsingConfiguredDayWindow) {
  auto store = std::make_shared<FakeWorkflowStore>();
  tcs::EventStorageManagementService service(store, 14);

  const auto outcome = service.prune_stale_boot_history();
  EXPECT_TRUE(outcome.performed);
  EXPECT_EQ(14, outcome.prune_after_days);
  EXPECT_EQ(1, store->prune_call_count);
  EXPECT_EQ(store->last_cutoff_unix_ms, outcome.cutoff_unix_ms);
  EXPECT_EQ(2, outcome.result.pruned_event_rows);
  EXPECT_EQ(1, outcome.result.pruned_plan_version_rows);
  EXPECT_EQ(3, outcome.result.pruned_audit_entry_rows);
  EXPECT_EQ(4, outcome.result.pruned_idempotency_rows);
}

}  // namespace
