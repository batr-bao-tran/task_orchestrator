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

  /** @brief Publish a workflow update event. */
  virtual WorkflowUpdateEvent publish(std::string workflow_id) = 0;
  /** @brief Get the ID of the latest published event. */
  virtual std::uint64_t latest_event_id() const = 0;
  /** @brief Block until a new event is published after the specified ID or the timeout elapses. */
  virtual std::optional<WorkflowUpdateEvent> wait_for_update(std::uint64_t after_event_id,
                                                             std::chrono::milliseconds timeout) = 0;

  /** @brief Wake all threads blocked in wait_for_update so they can observe shutdown. */
  virtual void shutdown() = 0;
};

std::shared_ptr<WorkflowUpdateFeed> make_in_memory_workflow_update_feed(std::size_t retained_events = 256);

}  // namespace task_orchestrator::control_plane::service

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_SERVICE__WORKFLOW_UPDATE_FEED_HPP_
