#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__NLP_PARSER_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__NLP_PARSER_HPP_
#include <string>
#include <string_view>

#include "task_orchestrator/optimizer/model.hpp"

namespace task_orchestrator::optimizer {

struct ParseResult {
  bool ok = false;
  OptimizationModel model;
  std::string error_message;
};

ParseResult parse_natural_language_request(std::string_view request);

}  // namespace task_orchestrator::optimizer

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_OPTIMIZER__NLP_PARSER_HPP_
