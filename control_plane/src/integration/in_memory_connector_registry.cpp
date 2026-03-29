#include <algorithm>

#include "control_plane/integration/connector_registry.hpp"

namespace task_orchestrator::control_plane::integration {

void InMemoryConnectorRegistry::upsert_binding(const ConnectorBinding& binding) {
  const auto existing =
      std::ranges::find_if(bindings_, [&](const ConnectorBinding& candidate) { return candidate.id == binding.id; });
  if (existing == bindings_.end()) {
    bindings_.push_back(binding);
    return;
  }
  *existing = binding;
}

std::vector<ConnectorBinding> InMemoryConnectorRegistry::list_bindings() const { return bindings_; }

bool InMemoryConnectorRegistry::has_binding(std::string_view id) const {
  return std::ranges::any_of(bindings_, [&](const ConnectorBinding& binding) { return binding.id == id; });
}

std::shared_ptr<ConnectorRegistry> make_in_memory_connector_registry() {
  return std::make_shared<InMemoryConnectorRegistry>();
}

}  // namespace task_orchestrator::control_plane::integration
