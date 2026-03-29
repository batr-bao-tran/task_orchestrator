#include "control_plane/service/event_storage_management_service.hpp"

#include <chrono>

namespace task_orchestrator::control_plane::service {
namespace {

constexpr std::int64_t kMsPerDay = 24LL * 60LL * 60LL * 1000LL;

std::int64_t unix_time_ms_now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

EventStorageManagementService::EventStorageManagementService(std::shared_ptr<store::WorkflowStore> workflow_store,
                                                             const std::int32_t prune_after_days)
    : workflow_store_(std::move(workflow_store)), prune_after_days_(prune_after_days) {}

EventStoragePruneOutcome EventStorageManagementService::prune_stale_boot_history() {
  EventStoragePruneOutcome outcome;
  outcome.prune_after_days = prune_after_days_;
  if (workflow_store_ == nullptr || prune_after_days_ <= 0) {
    return outcome;
  }

  outcome.performed = true;
  outcome.cutoff_unix_ms = unix_time_ms_now() - (static_cast<std::int64_t>(prune_after_days_) * kMsPerDay);
  outcome.result = workflow_store_->prune_stale_boot_data(outcome.cutoff_unix_ms);
  return outcome;
}

}  // namespace task_orchestrator::control_plane::service
