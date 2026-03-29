#ifndef TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_INTEGRATION__CONNECTOR_REGISTRY_HPP_
#define TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_INTEGRATION__CONNECTOR_REGISTRY_HPP_

#include <memory>
#include <string_view>
#include <vector>

#include "control_plane/integration/adapter.hpp"

namespace task_orchestrator::control_plane::integration {

class ConnectorRegistry {
 public:
  virtual ~ConnectorRegistry() noexcept = default;

  virtual void upsert_binding(const ConnectorBinding& binding) = 0;
  virtual std::vector<ConnectorBinding> list_bindings() const = 0;
  virtual bool has_binding(std::string_view id) const = 0;
};

class InMemoryConnectorRegistry final : public ConnectorRegistry {
 public:
  InMemoryConnectorRegistry() = default;
  ~InMemoryConnectorRegistry() noexcept override = default;

  void upsert_binding(const ConnectorBinding& binding) override;
  std::vector<ConnectorBinding> list_bindings() const override;
  bool has_binding(std::string_view id) const override;

 private:
  std::vector<ConnectorBinding> bindings_;
};

std::shared_ptr<ConnectorRegistry> make_in_memory_connector_registry();

}  // namespace task_orchestrator::control_plane::integration

#endif  // TASK_ORCHESTRATOR__CONTROL_PLANE_INCLUDE_CONTROL_PLANE_INTEGRATION__CONNECTOR_REGISTRY_HPP_
