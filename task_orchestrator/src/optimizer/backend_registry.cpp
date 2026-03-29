#include <algorithm>
#include <array>
#include <iterator>
#include <ranges>
#include <unordered_map>
#include <utility>

#include "task_orchestrator/optimizer/backend.hpp"
#include "utils/logger.hpp"

namespace task_orchestrator::optimizer {

std::string backend_kind_to_string(BackendKind kind);

namespace {

struct BackendEntry {
  BackendDescriptor descriptor;
  BackendFactoryFn factory;
};

constexpr std::array<BackendKind, 3> kBackendAutoOrder = {
    BackendKind::OrToolsCpSat,
    BackendKind::CommercialMip,
    BackendKind::IndexedExact,
};

std::vector<BackendEntry>& backend_entries() {
  static std::vector<BackendEntry> entries;
  return entries;
}

const BackendEntry* find_backend(BackendKind kind) {
  auto& entries = backend_entries();
  const auto it =
      std::ranges::find_if(entries, [kind](const BackendEntry& entry) { return entry.descriptor.kind == kind; });
  return it == entries.end() ? nullptr : &*it;
}

std::vector<BackendDescriptor> supported_backend_catalog() {
  return {
      BackendDescriptor{.kind = BackendKind::CommercialMip,
                        .name = "commercial_mip",
                        .available = false,
                        .optional_dependency = true},
      BackendDescriptor{
          .kind = BackendKind::IndexedExact, .name = "indexed_exact", .available = true, .optional_dependency = false},
      BackendDescriptor{
          .kind = BackendKind::OrToolsCpSat, .name = "ortools_cp_sat", .available = false, .optional_dependency = true},
  };
}

std::unique_ptr<OptimizationBackend> create_backend_for_registered_kind(BackendKind kind, std::string* error_message) {
  const BackendEntry* entry = find_backend(kind);
  if (!entry) {
    if (error_message) {
      *error_message = "Requested backend '" + backend_kind_to_string(kind) + "' is not linked into this binary.";
    }
    get_logger(LogLayer::Optimizer)
        ->warn("Requested backend '{}' is not linked into this binary.", backend_kind_to_string(kind));
    return nullptr;
  }
  if (!entry->descriptor.available) {
    if (error_message) {
      *error_message = "Requested backend '" + entry->descriptor.name + "' is registered but not available.";
    }
    get_logger(LogLayer::Optimizer)
        ->warn("Requested backend '{}' is registered but not available.", entry->descriptor.name);
    return nullptr;
  }
  return entry->factory ? entry->factory() : nullptr;
}

}  // namespace

void register_backend(BackendDescriptor descriptor, BackendFactoryFn factory) {
  auto& entries = backend_entries();
  const auto it = std::ranges::find_if(
      entries, [&descriptor](const BackendEntry& entry) { return entry.descriptor.kind == descriptor.kind; });
  if (it == entries.end()) {
    entries.push_back({.descriptor = std::move(descriptor), .factory = std::move(factory)});
  } else {
    it->descriptor = std::move(descriptor);
    it->factory = std::move(factory);
  }
}

std::vector<BackendDescriptor> list_registered_backends() {
  std::vector<BackendDescriptor> descriptors;
  const auto& entries = backend_entries();
  descriptors.reserve(entries.size());
  std::ranges::transform(entries, std::back_inserter(descriptors), [](const BackendEntry& backend_entry) {
    return backend_entry.descriptor;
  });
  std::ranges::sort(descriptors,
                    [](const BackendDescriptor& lhs, const BackendDescriptor& rhs) { return lhs.name < rhs.name; });
  return descriptors;
}

std::vector<BackendDescriptor> list_supported_backends() {
  std::vector<BackendDescriptor> descriptors = supported_backend_catalog();
  for (BackendDescriptor& descriptor : descriptors) {
    if (const BackendEntry* entry = find_backend(descriptor.kind); entry != nullptr) {
      descriptor = entry->descriptor;
    }
  }
  std::ranges::sort(descriptors,
                    [](const BackendDescriptor& lhs, const BackendDescriptor& rhs) { return lhs.name < rhs.name; });
  return descriptors;
}

BackendKind backend_kind_from_string(const std::string& value) {
  if (value == "indexed_exact" || value == "indexed_branch_and_bound") {
    return BackendKind::IndexedExact;
  }
  if (value == "ortools_cp_sat" || value == "cp_sat" || value == "ortools") {
    return BackendKind::OrToolsCpSat;
  }
  if (value == "commercial_mip" || value == "mip") {
    return BackendKind::CommercialMip;
  }
  return BackendKind::Auto;
}

std::string backend_kind_to_string(BackendKind kind) {
  switch (kind) {
    case BackendKind::IndexedExact:
      return "indexed_exact";
    case BackendKind::OrToolsCpSat:
      return "ortools_cp_sat";
    case BackendKind::CommercialMip:
      return "commercial_mip";
    case BackendKind::Auto:
    default:
      return "auto";
  }
}

std::unique_ptr<OptimizationBackend> create_backend(BackendKind kind, std::string* error_message) {
  if (kind == BackendKind::Auto) {
    for (const BackendKind candidate_kind : kBackendAutoOrder) {
      if (std::unique_ptr<OptimizationBackend> backend = create_backend_for_registered_kind(candidate_kind, nullptr)) {
        return backend;
      }
    }
    if (error_message) {
      *error_message = "No optimization backend is available.";
    }
    get_logger(LogLayer::Optimizer)->error("No optimization backend is available.");
    return nullptr;
  }
  return create_backend_for_registered_kind(kind, error_message);
}

std::unique_ptr<OptimizationBackend> create_backend(const OptimizationOptions& options, std::string* error_message) {
  return create_backend(options.backend_kind, error_message);
}

}  // namespace task_orchestrator::optimizer
