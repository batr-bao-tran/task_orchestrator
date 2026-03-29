#include <gtest/gtest.h>

#include "control_plane/integration/connector_registry.hpp"

namespace {
namespace tcp = task_orchestrator::control_plane;

TEST(ConnectorRegistryTest, UpsertsBindingsAndReportsPresence) {
  auto registry = tcp::integration::make_in_memory_connector_registry();

  tcp::integration::ConnectorBinding webhook{
      .id = "warehouse-webhook",
      .kind = tcp::integration::ConnectorKind::WebhookSource,
      .display_name = "Warehouse Webhook",
      .target = "/hooks/orders",
      .enabled = true,
  };
  registry->upsert_binding(webhook);

  tcp::integration::ConnectorBinding callback{
      .id = "robot-callback",
      .kind = tcp::integration::ConnectorKind::OutboundCallback,
      .display_name = "Robot Callback",
      .target = "https://robots.example.internal/events",
      .enabled = false,
  };
  registry->upsert_binding(callback);

  webhook.target = "/hooks/orders/v2";
  registry->upsert_binding(webhook);

  EXPECT_TRUE(registry->has_binding("warehouse-webhook"));
  EXPECT_FALSE(registry->has_binding("missing"));

  const auto bindings = registry->list_bindings();
  ASSERT_EQ(2U, bindings.size());
  EXPECT_EQ("/hooks/orders/v2", bindings.front().target);
}

}  // namespace
