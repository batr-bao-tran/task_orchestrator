#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__BACKEND_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__BACKEND_HPP_
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "task_orchestrator/optimizer/constraint_index.hpp"

namespace task_orchestrator::optimizer {

inline constexpr int64_t kDefaultFulfilledTaskWeight = 1000000;
inline constexpr int64_t kDefaultPriorityWeight = 1000;
inline constexpr int64_t kDefaultMakespanWeight = -1;
inline constexpr int64_t kDefaultTravelDistanceWeight = -1;
inline constexpr int64_t kDefaultTardinessWeight = -100;
inline constexpr int64_t kDefaultExecutionCostWeight = -1;
inline constexpr int64_t kDefaultPreferredActorWeight = 10;

enum class BackendKind {
  Auto,
  IndexedExact,
  OrToolsCpSat,
  CommercialMip,
};

struct ObjectiveWeights {
  int64_t fulfilled_task_weight = kDefaultFulfilledTaskWeight;
  int64_t priority_weight = kDefaultPriorityWeight;
  int64_t makespan_weight = kDefaultMakespanWeight;
  int64_t travel_distance_weight = kDefaultTravelDistanceWeight;
  int64_t tardiness_weight = kDefaultTardinessWeight;
  int64_t execution_cost_weight = kDefaultExecutionCostWeight;
  int64_t preferred_actor_weight = kDefaultPreferredActorWeight;
};

struct OptimizationOptions {
  BackendKind backend_kind = BackendKind::Auto;
  int64_t time_limit_ms = 0;
  double relative_gap_limit = 0.0;
  int num_search_workers = 1;
  bool allow_partial_plan = true;
  ObjectiveWeights objective;
};

struct BackendDescriptor {
  BackendKind kind = BackendKind::IndexedExact;
  std::string name;
  bool available = true;
  bool optional_dependency = false;
};

class OptimizationBackend {
 public:
  virtual ~OptimizationBackend() noexcept = default;
  virtual OptimizationSolution solve(const OptimizationModel& model,
                                     const ConstraintIndex& index,
                                     const OptimizationOptions& options) const = 0;
  virtual const char* name() const = 0;
};

using BackendFactoryFn = std::function<std::unique_ptr<OptimizationBackend>()>;

void register_backend(BackendDescriptor descriptor, BackendFactoryFn factory);
std::vector<BackendDescriptor> list_registered_backends();
std::vector<BackendDescriptor> list_supported_backends();
BackendKind backend_kind_from_string(const std::string& value);
std::string backend_kind_to_string(BackendKind kind);
std::unique_ptr<OptimizationBackend> create_backend(BackendKind kind, std::string* error_message = nullptr);
std::unique_ptr<OptimizationBackend> create_backend(const OptimizationOptions& options,
                                                    std::string* error_message = nullptr);

}  // namespace task_orchestrator::optimizer

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__BACKEND_HPP_
