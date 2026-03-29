#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "ortools/linear_solver/linear_solver.h"
#include "task_orchestrator/optimizer/discrete_time_formulation.hpp"
#include "utils/logger.hpp"

namespace task_orchestrator::optimizer {
namespace {

static constexpr double kSelectedOptionThreshold = 0.5;

struct SolverEngine {
  operations_research::MPSolver::OptimizationProblemType problem_type;
  const char* create_solver_id;
  const char* display_name;
};

const std::vector<SolverEngine>& mip_solver_preference_order() {
  static const std::vector<SolverEngine> kSolverPreferenceOrder = {
      {operations_research::MPSolver::GUROBI_MIXED_INTEGER_PROGRAMMING, "GUROBI", "GUROBI"},
      {operations_research::MPSolver::SCIP_MIXED_INTEGER_PROGRAMMING, "SCIP", "SCIP"},
      {operations_research::MPSolver::CBC_MIXED_INTEGER_PROGRAMMING, "CBC", "CBC"},
  };
  return kSolverPreferenceOrder;
}

const SolverEngine* select_available_solver_engine() {
  const auto available_engine_it = std::ranges::find_if(mip_solver_preference_order(), [](const SolverEngine& engine) {
    return operations_research::MPSolver::SupportsProblemType(engine.problem_type);
  });
  return available_engine_it == mip_solver_preference_order().end() ? nullptr : &*available_engine_it;
}

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

class CommercialMipBackend final : public OptimizationBackend {
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

    namespace ort = operations_research;
    const SolverEngine* solver_engine = select_available_solver_engine();
    if (solver_engine == nullptr) {
      return OptimizationSolution{
          .ok = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .backend_name = name(),
          .stats = {},
          .error_message =
              "MIP backend requires an OR-Tools build with at least one mixed-integer solver engine (GUROBI, SCIP, or "
              "CBC).",
      };
    }

    if (std::string_view(solver_engine->display_name) != "GUROBI") {
      get_logger(LogLayer::Optimizer)
          ->warn("Commercial MIP backend requested GUROBI but is using {} as the available MIP engine.",
                 solver_engine->display_name);
    }

    std::unique_ptr<ort::MPSolver> solver(ort::MPSolver::CreateSolver(solver_engine->create_solver_id));
    if (!solver) {
      return OptimizationSolution{
          .ok = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .backend_name = name(),
          .stats = {},
          .error_message = "MIP backend could not create the selected " + std::string(solver_engine->display_name) +
                           " solver. Check the runtime environment and optional solver installation.",
      };
    }

    if (options.time_limit_ms > 0) {
      solver->set_time_limit(options.time_limit_ms);
    }
    if (options.num_search_workers > 0) {
      const auto thread_status = solver->SetNumThreads(options.num_search_workers);
      if (!thread_status.ok()) {
        get_logger(LogLayer::Optimizer)
            ->warn("Commercial MIP backend could not apply thread setting: {}", thread_status.ToString());
      }
    }

    std::vector<const ort::MPVariable*> option_decision_variables;
    option_decision_variables.reserve(formulation.options.size());
    std::ranges::transform(
        formulation.options, std::back_inserter(option_decision_variables), [&solver](const DiscreteOption& option) {
          return solver->MakeBoolVar(option_variable_name(option));
        });

    const auto set_constraint_coefficients =
        [&formulation, &option_decision_variables](
            ort::MPConstraint* constraint, const std::vector<size_t>& option_indexes, auto coefficient_for_option) {
          std::ranges::for_each(option_indexes, [&](const size_t option_index) {
            constraint->SetCoefficient(option_decision_variables[option_index],
                                       coefficient_for_option(formulation.options[option_index]));
          });
        };

    for (const DiscreteTaskDecision& task_decision : formulation.task_decisions) {
      ort::MPConstraint* selection_constraint = solver->MakeRowConstraint(
          0.0, task_decision.required ? 1.0 : 1.0, "task_" + task_decision.task_id + "_selection");
      if (task_decision.required) {
        selection_constraint->SetBounds(1.0, 1.0);
      }
      set_constraint_coefficients(
          selection_constraint, task_decision.option_indexes, [](const DiscreteOption&) { return 1.0; });
    }

    for (const ActorTimeSlotConstraint& time_slot_constraint : formulation.actor_time_slots) {
      ort::MPConstraint* capacity_constraint = solver->MakeRowConstraint(
          -solver->infinity(),
          static_cast<double>(time_slot_constraint.capacity),
          "cap_" + time_slot_constraint.actor_id + "_" + std::to_string(time_slot_constraint.time));
      set_constraint_coefficients(capacity_constraint,
                                  time_slot_constraint.option_indexes,
                                  [](const DiscreteOption& option) { return static_cast<double>(option.demand); });
    }

    const double big_m = static_cast<double>(formulation.horizon_end);
    for (const DependencyConstraint& dependency_constraint : formulation.dependencies) {
      const DiscreteTaskDecision& predecessor =
          formulation.task_decisions[dependency_constraint.predecessor_task_index];
      const DiscreteTaskDecision& successor = formulation.task_decisions[dependency_constraint.successor_task_index];

      ort::MPConstraint* presence_constraint = solver->MakeRowConstraint(
          -solver->infinity(), 0.0, "dep_presence_" + predecessor.task_id + "_" + successor.task_id);
      set_constraint_coefficients(
          presence_constraint, successor.option_indexes, [](const DiscreteOption&) { return 1.0; });
      set_constraint_coefficients(
          presence_constraint, predecessor.option_indexes, [](const DiscreteOption&) { return -1.0; });

      ort::MPConstraint* timing_constraint = solver->MakeRowConstraint(
          -big_m, solver->infinity(), "dep_time_" + predecessor.task_id + "_" + successor.task_id);
      set_constraint_coefficients(timing_constraint, successor.option_indexes, [big_m](const DiscreteOption& option) {
        return static_cast<double>(option.start_time) - big_m;
      });
      set_constraint_coefficients(timing_constraint, predecessor.option_indexes, [](const DiscreteOption& option) {
        return -static_cast<double>(option.end_time);
      });
    }

    for (const MutualExclusionConstraint& mutual_exclusion_constraint : formulation.mutual_exclusions) {
      const DiscreteTaskDecision& first = formulation.task_decisions[mutual_exclusion_constraint.first_task_index];
      const DiscreteTaskDecision& second = formulation.task_decisions[mutual_exclusion_constraint.second_task_index];
      ort::MPConstraint* mutex_row =
          solver->MakeRowConstraint(-solver->infinity(), 1.0, "mutex_" + first.task_id + "_" + second.task_id);
      set_constraint_coefficients(mutex_row, first.option_indexes, [](const DiscreteOption&) { return 1.0; });
      set_constraint_coefficients(mutex_row, second.option_indexes, [](const DiscreteOption&) { return 1.0; });
    }

    ort::MPVariable* makespan = nullptr;
    if (options.objective.makespan_weight != 0 && formulation.horizon_end > 0) {
      makespan = solver->MakeIntVar(0.0, static_cast<double>(formulation.horizon_end), "makespan");
      for (const DiscreteTaskDecision& task_decision : formulation.task_decisions) {
        ort::MPConstraint* makespan_constraint =
            solver->MakeRowConstraint(0.0, solver->infinity(), "mk_" + task_decision.task_id);
        makespan_constraint->SetCoefficient(makespan, 1.0);
        set_constraint_coefficients(makespan_constraint,
                                    task_decision.option_indexes,
                                    [](const DiscreteOption& option) { return -static_cast<double>(option.end_time); });
      }
    }

    ort::MPObjective* objective = solver->MutableObjective();
    objective->SetMaximization();
    for (const DiscreteOption& option : formulation.options) {
      objective->SetCoefficient(option_decision_variables[option.option_index],
                                static_cast<double>(option.objective_coefficient));
    }
    if (makespan != nullptr) {
      objective->SetCoefficient(makespan, static_cast<double>(options.objective.makespan_weight));
    }

    ort::MPSolverParameters parameters;
    if (options.relative_gap_limit > 0.0) {
      parameters.SetDoubleParam(ort::MPSolverParameters::RELATIVE_MIP_GAP, options.relative_gap_limit);
    }

    const ort::MPSolver::ResultStatus result_status = solver->Solve(parameters);
    const OptimizationStats stats{
        .search_nodes = static_cast<size_t>(std::max<int64_t>(solver->nodes(), 0)),
        .pruned_nodes = 0,
    };
    if (result_status != ort::MPSolver::OPTIMAL && result_status != ort::MPSolver::FEASIBLE) {
      return OptimizationSolution{
          .ok = false,
          .assignments = {},
          .unfulfilled_task_ids = {},
          .backend_name = std::string(name()) + "[" + solver_engine->display_name + "]",
          .stats = stats,
          .error_message = "MIP solve failed with engine " + std::string(solver_engine->display_name) +
                           " and status code " + std::to_string(static_cast<int>(result_status)) + ".",
      };
    }

    const std::vector<size_t> selected_option_indexes =
        collect_selected_option_indexes(option_decision_variables, [](const ort::MPVariable* option_decision_variable) {
          return option_decision_variable->solution_value() > kSelectedOptionThreshold;
        });
    return decode_selected_options(
        formulation, selected_option_indexes, std::string(name()) + "[" + solver_engine->display_name + "]", stats);
  }

  const char* name() const override { return "commercial_mip"; }
};

const bool kCommercialMipBackendRegistered = []() {
  register_backend(
      BackendDescriptor{
          .kind = BackendKind::CommercialMip,
          .name = "commercial_mip",
          .available = true,
          .optional_dependency = true,
      },
      []() { return std::make_unique<CommercialMipBackend>(); });
  return true;
}();

}  // namespace
}  // namespace task_orchestrator::optimizer
