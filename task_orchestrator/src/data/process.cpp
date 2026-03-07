#include "task_orchestrator/data/process.hpp"

namespace task_orchestrator {

std::vector<TaskId> Process::task_ids() const {
  std::vector<TaskId> out;
  out.push_back(id);  // process itself is a task
  for (const auto& sp : sub_process_ids) {
    out.push_back(sp);
  }
  return out;
}

}  // namespace task_orchestrator
