#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

#include "ortools/sat/cp_model.h"
#include "task_orchestrator/optimizer/discrete_time_formulation.hpp"

namespace task_orchestrator::optimizer {
namespace {

static constexpr double kMillisecondsPerSecond = 1000.0;

std::string option_variable_name(const DiscreteOption& option) {
  return "opt_" + option.task_id + "_" + option.actor_id + "_" + std::to_string(option.start_time);
}

const DiscreteTaskDecision* first_required_task_without_options(const DiscreteTimeFormulation& formulation) {
  const auto required_task_it =
      std::ranges::find_if(formulation.task_decisions, [](const DiscreteTaskDecision& task_decision) {
        return task_decision.required && task_decision.option_indexes.empty();
      });
  return required_task_it == formulation.task_decisions.end() ? nullptr : &*required_task_it;
}

template <typename OptionVariableContainer, typename IsSelectedFn>
std::vector<size_t> collect_selected_option_indexes(const OptionVariableContainer& option_decision_variables,
                                                    IsSelectedFn&& is_selected) {
  std::vector<size_t> selected_option_indexes;
  selected_option_indexes.reserve(option_decision_variables.size());
  for (size_t option_index = 0; option_index < option_decision_variables.size(); ++option_index) {
    if (is_selected(option_decision_variables[option_index])) {
      selected_option_indexes.push_back(option_index);
    }
  }
  return selected_option_indexes;
}

class OrToolsCpSatBackend final : public OptimizationBackend {
 public:
  OptimizationSolution solve(const OptimizationModel& model,
                             const ConstraintIndex& index,
                             const OptimizationOptions& options) const override {
    std::string validation_error;
    if (!backend_model_supported(model, &validation_error)) {
      return OptimizationSolution{
          .ok = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .backend_name = name(),
          .stats = {},
          .error_message = validation_error,
      };
    }

    const DiscreteTimeFormulation formulation = build_discrete_time_formulation(model, index, options);
    if (const DiscreteTaskDecision* required_task = first_required_task_without_options(formulation);
        required_task != nullptr) {
      return OptimizationSolution{
          .ok = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .backend_name = name(),
          .stats = {},
          .error_message = "Required task '" + required_task->task_id + "' has no feasible assignment options.",
      };
    }

    namespace ort = operations_research::sat;

    ort::CpModelBuilder model_builder;
    std::vector<ort::BoolVar> option_decision_variables;
    option_decision_variables.reserve(formulation.options.size());
    std::ranges::transform(formulation.options,
                           std::back_inserter(option_decision_variables),
                           [&model_builder](const DiscreteOption& option) {
                             return model_builder.NewBoolVar().WithName(option_variable_name(option));
                           });

    std::vector<ort::LinearExpr> task_selected(formulation.task_decisions.size(), ort::LinearExpr(0));
    std::vector<ort::LinearExpr> task_start(formulation.task_decisions.size(), ort::LinearExpr(0));
    std::vector<ort::LinearExpr> task_end(formulation.task_decisions.size(), ort::LinearExpr(0));

    for (const DiscreteTaskDecision& task_decision : formulation.task_decisions) {
      ort::LinearExpr select_expr(0);
      ort::LinearExpr start_expr(0);
      ort::LinearExpr end_expr(0);
      for (const size_t option_index : task_decision.option_indexes) {
        const DiscreteOption& option = formulation.options[option_index];
        select_expr += option_decision_variables[option_index];
        start_expr += static_cast<int64_t>(option.start_time) * option_decision_variables[option_index];
        end_expr += static_cast<int64_t>(option.end_time) * option_decision_variables[option_index];
      }
      if (task_decision.required) {
        model_builder.AddEquality(select_expr, 1);
      } else {
        model_builder.AddLessOrEqual(select_expr, 1);
      }
      task_selected[task_decision.task_index] = select_expr;
      task_start[task_decision.task_index] = start_expr;
      task_end[task_decision.task_index] = end_expr;
    }

    for (const ActorTimeSlotConstraint& time_slot_constraint : formulation.actor_time_slots) {
      ort::LinearExpr load_expr(0);
      for (const size_t option_index : time_slot_constraint.option_indexes) {
        load_expr +=
            static_cast<int64_t>(formulation.options[option_index].demand) * option_decision_variables[option_index];
      }
      model_builder.AddLessOrEqual(load_expr, time_slot_constraint.capacity);
    }

    const int64_t big_m = static_cast<int64_t>(formulation.horizon_end);
    for (const DependencyConstraint& dependency_constraint : formulation.dependencies) {
      model_builder.AddLessOrEqual(task_selected[dependency_constraint.successor_task_index],
                                   task_selected[dependency_constraint.predecessor_task_index]);
      model_builder.AddGreaterOrEqual(task_start[dependency_constraint.successor_task_index] -
                                          task_end[dependency_constraint.predecessor_task_index] -
                                          big_m * task_selected[dependency_constraint.successor_task_index],
                                      -big_m);
    }

    for (const MutualExclusionConstraint& mutual_exclusion_constraint : formulation.mutual_exclusions) {
      model_builder.AddLessOrEqual(task_selected[mutual_exclusion_constraint.first_task_index] +
                                       task_selected[mutual_exclusion_constraint.second_task_index],
                                   1);
    }

    ort::LinearExpr objective_expr(0);
    for (const DiscreteOption& option : formulation.options) {
      objective_expr += option.objective_coefficient * option_decision_variables[option.option_index];
    }

    const bool use_makespan = options.objective.makespan_weight != 0 && formulation.horizon_end > 0;
    ort::IntVar makespan;
    if (use_makespan) {
      makespan = model_builder.NewIntVar(operations_research::Domain(0, formulation.horizon_end)).WithName("makespan");
      for (const DiscreteTaskDecision& task_decision : formulation.task_decisions) {
        model_builder.AddGreaterOrEqual(makespan, task_end[task_decision.task_index]);
      }
      objective_expr += options.objective.makespan_weight * makespan;
    }

    model_builder.Maximize(objective_expr);

    ort::SatParameters parameters;
    if (options.time_limit_ms > 0) {
      parameters.set_max_time_in_seconds(static_cast<double>(options.time_limit_ms) / kMillisecondsPerSecond);
    }
    if (options.relative_gap_limit > 0.0) {
      parameters.set_relative_gap_limit(options.relative_gap_limit);
    }
    if (options.num_search_workers > 0) {
      parameters.set_num_workers(options.num_search_workers);
    }

    const ort::CpSolverResponse response = ort::SolveWithParameters(model_builder.Build(), parameters);
    const OptimizationStats stats{
        .search_nodes = static_cast<size_t>(std::max<int64_t>(response.num_branches(), 0)),
        .pruned_nodes = static_cast<size_t>(std::max<int64_t>(response.num_conflicts(), 0)),
    };
    if (response.status() != ort::CpSolverStatus::OPTIMAL && response.status() != ort::CpSolverStatus::FEASIBLE) {
      return OptimizationSolution{
          .ok = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .backend_name = name(),
          .stats = stats,
          .error_message = "CP-SAT solve ended with status " + ort::CpSolverStatus_Name(response.status()) + ".",
      };
    }

    const std::vector<size_t> selected_option_indexes = collect_selected_option_indexes(
        option_decision_variables, [&response](const ort::BoolVar& option_decision_variable) {
          return ort::SolutionBooleanValue(response, option_decision_variable);
        });

    return decode_selected_options(formulation, selected_option_indexes, name(), stats);
  }

  const char* name() const override { return "ortools_cp_sat"; }
};

const bool kCpSatBackendRegistered = []() {
  register_backend(
      BackendDescriptor{
          .kind = BackendKind::OrToolsCpSat,
          .name = "ortools_cp_sat",
          .available = true,
          .optional_dependency = true,
      },
      []() { return std::make_unique<OrToolsCpSatBackend>(); });
  return true;
}();

}  // namespace
}  // namespace task_orchestrator::optimizer
