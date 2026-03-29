#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__OPTIMIZER_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__OPTIMIZER_HPP_
#include <memory>
#include <string_view>

#include "task_orchestrator/optimizer/backend.hpp"
#include "task_orchestrator/optimizer/nlp_parser.hpp"

namespace task_orchestrator::optimizer {

class WorkflowOptimizer {
 public:
  WorkflowOptimizer();
  explicit WorkflowOptimizer(OptimizationOptions options);
  explicit WorkflowOptimizer(std::unique_ptr<OptimizationBackend> backend);
  WorkflowOptimizer(std::unique_ptr<OptimizationBackend> backend, OptimizationOptions options);

  static ParseResult parse_text(std::string_view request);
  OptimizationSolution optimize(const OptimizationModel& model) const;
  OptimizationSolution optimize_text(std::string_view request) const;

 private:
  OptimizationOptions options_;
  std::unique_ptr<OptimizationBackend> backend_;
};

}  // namespace task_orchestrator::optimizer

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__OPTIMIZER_HPP_
