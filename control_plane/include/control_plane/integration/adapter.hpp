#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_INTEGRATION__ADAPTER_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_INTEGRATION__ADAPTER_HPP_

#include <string>
#include <string_view>

namespace task_orchestrator::control_plane::integration {

enum class ConnectorKind {
  WebhookSource,
  ScheduleSource,
  QueueConsumer,
  OutboundCallback,
  DomainConnector,
};

struct ConnectorBinding {
  std::string id;
  ConnectorKind kind = ConnectorKind::WebhookSource;
  std::string display_name;
  std::string target;
  bool enabled = true;
};

class InboundTriggerAdapter {
 public:
  virtual ~InboundTriggerAdapter() noexcept = default;

  virtual std::string_view id() const = 0;
  virtual ConnectorKind kind() const = 0;
};

class OutboundEventSink {
 public:
  virtual ~OutboundEventSink() noexcept = default;

  virtual std::string_view id() const = 0;
  virtual ConnectorKind kind() const = 0;
};

}  // namespace task_orchestrator::control_plane::integration

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_INTEGRATION__ADAPTER_HPP_
