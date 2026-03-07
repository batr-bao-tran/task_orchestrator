#include "task_orchestrator/core/workflow.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <unordered_set>

namespace task_orchestrator {

void Workflow::add_phase(Phase p) { phases_[p.id] = std::move(p); }

void Workflow::add_process(Process p) { processes_[p.id] = std::move(p); }

const Phase* Workflow::phase(const PhaseId& id) const {
  auto it = phases_.find(id);
  return it == phases_.end() ? nullptr : &it->second;
}

const Process* Workflow::process(const ProcessId& id) const {
  auto it = processes_.find(id);
  return it == processes_.end() ? nullptr : &it->second;
}

std::vector<PhaseId> Workflow::phase_ids() const {
  std::vector<PhaseId> out;
  auto keys = phases_ | std::ranges::views::transform([](const auto& kv) { return kv.first; });
  std::ranges::copy(keys, std::back_inserter(out));
  return out;
}

std::vector<ProcessId> Workflow::process_ids() const {
  std::vector<ProcessId> out;
  auto keys = processes_ | std::ranges::views::transform([](const auto& kv) { return kv.first; });
  std::ranges::copy(keys, std::back_inserter(out));
  return out;
}

std::vector<PhaseId> Workflow::root_phases() const {
  std::vector<PhaseId> out;
  for (const auto& [id, p] : phases_) {
    if (p.dependency_phase_ids.empty()) {
      out.push_back(id);
    }
  }
  return out;
}

std::vector<PhaseId> Workflow::ready_phases(const std::vector<PhaseId>& completed_phase_ids) const {
  std::unordered_set<PhaseId> completed(completed_phase_ids.begin(), completed_phase_ids.end());
  std::vector<PhaseId> out;
  for (const auto& [id, p] : phases_) {
    if (completed.count(id)) continue;
    bool all_deps_done = std::ranges::all_of(p.dependency_phase_ids,
                                             [&completed](const PhaseId& dep) { return completed.count(dep) != 0; });
    if (all_deps_done) {
      out.push_back(id);
    }
  }
  return out;
}

std::vector<TaskId> Workflow::task_ids_for_phase(const PhaseId& phase_id) const {
  const Phase* ph = phase(phase_id);
  if (!ph) return {};
  std::vector<TaskId> out;
  for (const ProcessId& pid : ph->process_ids) {
    const Process* proc = process(pid);
    if (!proc) continue;
    for (const TaskId& tid : proc->task_ids()) {
      out.push_back(tid);
    }
  }
  return out;
}

const Process* Workflow::process_for_task(const TaskId& task_id) const {
  const Process* p = process(task_id);
  if (p) return p;
  for (const auto& [_, proc] : processes_) {
    auto it = std::ranges::find(proc.sub_process_ids, task_id);
    if (it != proc.sub_process_ids.end()) return &proc;
  }
  return nullptr;
}

}  // namespace task_orchestrator
