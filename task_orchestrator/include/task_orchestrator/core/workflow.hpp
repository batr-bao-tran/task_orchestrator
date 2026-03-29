#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__WORKFLOW_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__WORKFLOW_HPP_
#include <unordered_map>
#include <vector>

#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "utils/types.hpp"

namespace task_orchestrator {

class Workflow {
 public:
  Workflow() = default;
  explicit Workflow(WorkflowId id) : id_(std::move(id)) {}

  void set_id(const WorkflowId& id) { id_ = id; }
  const WorkflowId& id() const { return id_; }

  void add_phase(Phase p);
  void add_process(Process p);

  const Phase* phase(const PhaseId& id) const;
  const Process* process(const ProcessId& id) const;
  std::vector<PhaseId> phase_ids() const;
  std::vector<ProcessId> process_ids() const;

  /** Phase IDs that have no dependencies (roots of DAG). */
  std::vector<PhaseId> root_phases() const;
  /** Phase IDs whose dependencies are all in completed_phase_ids. */
  std::vector<PhaseId> ready_phases(const std::vector<PhaseId>& completed_phase_ids) const;

  /** All task IDs (process + sub-process) for a phase. */
  std::vector<TaskId> task_ids_for_phase(const PhaseId& phase_id) const;

  /** Process that owns this task (task id is process id or sub_process id). */
  const Process* process_for_task(const TaskId& task_id) const;

 private:
  WorkflowId id_;
  std::unordered_map<PhaseId, Phase> phases_;
  std::unordered_map<ProcessId, Process> processes_;
};

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__WORKFLOW_HPP_
