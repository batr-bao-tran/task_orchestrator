#include "control_plane/service/workflow_update_feed.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace task_orchestrator::control_plane::service {
namespace {

TEST(WorkflowUpdateFeedTest, PublishesWaitsAndRetainsRecentEvents) {
  auto feed = make_in_memory_workflow_update_feed(2);

  EXPECT_EQ(0U, feed->latest_event_id());
  EXPECT_FALSE(feed->wait_for_update(0, std::chrono::milliseconds(1)).has_value());

  const auto first = feed->publish("wf-1");
  EXPECT_EQ(1U, first.event_id);
  EXPECT_EQ("wf-1", first.workflow_id);
  EXPECT_EQ(1U, feed->latest_event_id());

  const auto immediate = feed->wait_for_update(0, std::chrono::milliseconds(1));
  ASSERT_TRUE(immediate.has_value());
  EXPECT_EQ(1U, immediate->event_id);

  const auto second = feed->publish("wf-2");
  const auto third = feed->publish("wf-3");
  EXPECT_EQ(3U, third.event_id);

  const auto retained = feed->wait_for_update(1, std::chrono::milliseconds(1));
  ASSERT_TRUE(retained.has_value());
  EXPECT_EQ(second.event_id, retained->event_id);

  const auto trimmed = feed->wait_for_update(0, std::chrono::milliseconds(1));
  ASSERT_TRUE(trimmed.has_value());
  EXPECT_EQ(second.event_id, trimmed->event_id);
}

TEST(WorkflowUpdateFeedTest, UnblocksWaitingSubscriberWhenNewEventArrives) {
  auto feed = make_in_memory_workflow_update_feed();

  std::optional<WorkflowUpdateEvent> observed;
  std::jthread waiter([&]() { observed = feed->wait_for_update(0, std::chrono::milliseconds(250)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const auto published = feed->publish("wf-live");
  waiter.join();

  ASSERT_TRUE(observed.has_value());
  EXPECT_EQ(published.event_id, observed->event_id);
  EXPECT_EQ("wf-live", observed->workflow_id);
}

}  // namespace
}  // namespace task_orchestrator::control_plane::service
