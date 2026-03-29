#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PHASE_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PHASE_HPP_
#include <vector>

#include "task_orchestrator/data/process.hpp"
#include "utils/types.hpp"

namespace task_orchestrator {

struct Phase {
  PhaseId id;
  std::string name;
  std::vector<ProcessId> process_ids;
  /** Predecessor phases; this phase is ready when all are complete. */
  std::vector<PhaseId> dependency_phase_ids;
};

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_DATA__PHASE_HPP_
