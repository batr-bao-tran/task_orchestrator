#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__DISCRETE_TIME_FORMULATION_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__DISCRETE_TIME_FORMULATION_HPP_
#include <cstddef>
#include <string>
#include <vector>

#include "task_orchestrator/optimizer/backend.hpp"

namespace task_orchestrator::optimizer {

inline constexpr int64_t kScaledDoubleFactor = 1000;
inline constexpr Time kUnknownDistancePadding = 1;

struct DiscreteOption {
  size_t option_index = 0;
  size_t task_index = 0;
  size_t actor_index = 0;
  TaskId task_id;
  ActorId actor_id;
  Time start_time = 0;
  Time end_time = 0;
  int demand = 1;
  int64_t objective_coefficient = 0;
};

struct DiscreteTaskDecision {
  size_t task_index = 0;
  TaskId task_id;
  bool required = false;
  std::vector<size_t> option_indexes;
};

struct ActorTimeSlotConstraint {
  size_t actor_index = 0;
  ActorId actor_id;
  Time time = 0;
  int capacity = 0;
  std::vector<size_t> option_indexes;
};

struct DependencyConstraint {
  size_t predecessor_task_index = 0;
  size_t successor_task_index = 0;
};

struct MutualExclusionConstraint {
  size_t first_task_index = 0;
  size_t second_task_index = 0;
};

struct DiscreteTimeFormulation {
  const OptimizationModel* model = nullptr;
  std::vector<DiscreteOption> options;
  std::vector<DiscreteTaskDecision> task_decisions;
  std::vector<ActorTimeSlotConstraint> actor_time_slots;
  std::vector<DependencyConstraint> dependencies;
  std::vector<MutualExclusionConstraint> mutual_exclusions;
  Time horizon_end = 0;
};

bool backend_model_supported(const OptimizationModel& model, std::string* error_message);
DiscreteTimeFormulation build_discrete_time_formulation(const OptimizationModel& model,
                                                        const ConstraintIndex& index,
                                                        const OptimizationOptions& options);
OptimizationSolution decode_selected_options(const DiscreteTimeFormulation& formulation,
                                             const std::vector<size_t>& selected_option_indexes,
                                             std::string backend_name,
                                             OptimizationStats stats = {});

}  // namespace task_orchestrator::optimizer

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__DISCRETE_TIME_FORMULATION_HPP_
