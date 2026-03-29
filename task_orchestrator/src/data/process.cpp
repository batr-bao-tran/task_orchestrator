#include "task_orchestrator/data/process.hpp"

namespace task_orchestrator {

std::vector<TaskId> Process::task_ids() const {
  std::vector<TaskId> task_ids;
  task_ids.reserve(sub_process_ids.size() + 1);
  task_ids.push_back(id);  // process itself is a task
  task_ids.insert(task_ids.end(), sub_process_ids.begin(), sub_process_ids.end());
  return task_ids;
}

}  // namespace task_orchestrator
