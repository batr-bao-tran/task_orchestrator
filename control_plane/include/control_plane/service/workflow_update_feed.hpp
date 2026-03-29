#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__WORKFLOW_UPDATE_FEED_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__WORKFLOW_UPDATE_FEED_HPP_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace task_orchestrator::control_plane::service {

struct WorkflowUpdateEvent {
  std::uint64_t event_id = 0;
  std::string workflow_id;
};

class WorkflowUpdateFeed {
 public:
  virtual ~WorkflowUpdateFeed() noexcept = default;

  virtual WorkflowUpdateEvent publish(std::string workflow_id) = 0;
  virtual std::uint64_t latest_event_id() const = 0;
  virtual std::optional<WorkflowUpdateEvent> wait_for_update(std::uint64_t after_event_id,
                                                             std::chrono::milliseconds timeout) = 0;
};

std::shared_ptr<WorkflowUpdateFeed> make_in_memory_workflow_update_feed(std::size_t retained_events = 256);

}  // namespace task_orchestrator::control_plane::service

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__WORKFLOW_UPDATE_FEED_HPP_
