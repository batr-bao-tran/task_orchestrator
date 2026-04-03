#include "control_plane/service/workflow_update_feed.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace task_orchestrator::control_plane::service {

namespace {

class InMemoryWorkflowUpdateFeed final : public WorkflowUpdateFeed {
 public:
  explicit InMemoryWorkflowUpdateFeed(std::size_t retained_events)
      : retained_events_(std::max<std::size_t>(1U, retained_events)) {}

  WorkflowUpdateEvent publish(std::string workflow_id) override {
    std::scoped_lock lock(mutex_);
    WorkflowUpdateEvent event{
        .event_id = ++latest_event_id_,
        .workflow_id = std::move(workflow_id),
    };
    events_.push_back(event);
    while (events_.size() > retained_events_) {
      events_.pop_front();
    }
    condition_variable_.notify_all();
    return event;
  }

  std::uint64_t latest_event_id() const override {
    std::scoped_lock lock(mutex_);
    return latest_event_id_;
  }

  std::optional<WorkflowUpdateEvent> wait_for_update(const std::uint64_t after_event_id,
                                                     const std::chrono::milliseconds timeout) override {
    std::unique_lock lock(mutex_);
    const auto ready = [this, after_event_id]() -> bool {
      return shutting_down_.load() || latest_event_id_ > after_event_id;
    };
    if (!ready() && !condition_variable_.wait_for(lock, timeout, ready)) {
      return std::nullopt;
    }
    if (shutting_down_) {
      return std::nullopt;
    }

    const auto newer_event = std::ranges::find_if(
        events_, [after_event_id](const WorkflowUpdateEvent& event) { return event.event_id > after_event_id; });
    if (newer_event == events_.end()) {
      return std::nullopt;
    }
    return *newer_event;
  }

  void shutdown() override {
    std::scoped_lock lock(mutex_);
    shutting_down_ = true;
    condition_variable_.notify_all();
  }

 private:
  const std::size_t retained_events_;
  mutable std::mutex mutex_;
  std::condition_variable condition_variable_;
  std::deque<WorkflowUpdateEvent> events_;
  std::uint64_t latest_event_id_ = 0;
  std::atomic<bool> shutting_down_ = false;
};

}  // namespace

std::shared_ptr<WorkflowUpdateFeed> make_in_memory_workflow_update_feed(const std::size_t retained_events) {
  return std::make_shared<InMemoryWorkflowUpdateFeed>(retained_events);
}

}  // namespace task_orchestrator::control_plane::service
