#include <memory>
#include <string>
#include <utility>

#include "task_orchestrator/optimizer/constraint_index.hpp"
#include "task_orchestrator/optimizer/optimizer.hpp"
#include "utils/logger.hpp"

namespace task_orchestrator::optimizer {

WorkflowOptimizer::WorkflowOptimizer() : WorkflowOptimizer(OptimizationOptions{}) {}

WorkflowOptimizer::WorkflowOptimizer(OptimizationOptions options) : options_(std::move(options)) {}

WorkflowOptimizer::WorkflowOptimizer(std::unique_ptr<OptimizationBackend> backend)
    : WorkflowOptimizer(std::move(backend), OptimizationOptions{}) {}

WorkflowOptimizer::WorkflowOptimizer(std::unique_ptr<OptimizationBackend> backend, OptimizationOptions options)
    : options_(std::move(options)), backend_(std::move(backend)) {}

ParseResult WorkflowOptimizer::parse_text(std::string_view request) { return parse_natural_language_request(request); }

OptimizationSolution WorkflowOptimizer::optimize(const OptimizationModel& model) const {
  ConstraintIndex index(model);
  std::string backend_error;
  std::unique_ptr<OptimizationBackend> owned_backend;
  const OptimizationBackend* backend = backend_.get();
  if (!backend) {
    owned_backend = create_backend(options_, &backend_error);
    backend = owned_backend.get();
  }
  if (!backend) {
    get_logger(LogLayer::Optimizer)
        ->error("Workflow optimization failed because no backend is available: {}", backend_error);
    return OptimizationSolution{
        .ok = false,
        .assignments = {},
        .unfulfilled_task_ids = {},
        .backend_name = backend_kind_to_string(options_.backend_kind),
        .stats = {},
        .error_message = backend_error.empty() ? "No optimization backend available." : backend_error};
  }
  return backend->solve(model, index, options_);
}

OptimizationSolution WorkflowOptimizer::optimize_text(std::string_view request) const {
  const ParseResult parsed = parse_text(request);
  if (!parsed.ok) {
    return OptimizationSolution{
        .ok = false,
        .assignments = {},
        .unfulfilled_task_ids = {},
        .backend_name = "",
        .stats = {},
        .error_message = parsed.error_message,
    };
  }
  return optimize(parsed.model);
}

}  // namespace task_orchestrator::optimizer
