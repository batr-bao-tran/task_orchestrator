#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__EVENT_STORAGE_MANAGEMENT_SERVICE_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__EVENT_STORAGE_MANAGEMENT_SERVICE_HPP_

#include <cstdint>
#include <memory>

#include "control_plane/store/workflow_store.hpp"

namespace task_orchestrator::control_plane::service {

struct EventStoragePruneOutcome {
  bool performed = false;
  std::int32_t prune_after_days = 0;
  std::int64_t cutoff_unix_ms = 0;
  store::StoragePruneResult result;
};

class EventStorageManagementService {
 public:
  EventStorageManagementService(std::shared_ptr<store::WorkflowStore> workflow_store, std::int32_t prune_after_days);
  ~EventStorageManagementService() noexcept = default;

  [[nodiscard]] EventStoragePruneOutcome prune_stale_boot_history();

 private:
  std::shared_ptr<store::WorkflowStore> workflow_store_;
  std::int32_t prune_after_days_ = 0;
};

}  // namespace task_orchestrator::control_plane::service

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__EVENT_STORAGE_MANAGEMENT_SERVICE_HPP_
