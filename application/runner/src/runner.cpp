#include "runner/runner.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include "config/config.hpp"
#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"

namespace task_orchestrator::app {

namespace {
bool actor_type_matches(const std::string& actor_type, const std::vector<std::string>& allowed) {
  if (allowed.empty()) return true;
  return std::ranges::find(allowed, actor_type) != allowed.end();
}
}  // namespace

RunResult run(const WorkflowConfig& config) {
  RunResult result;
  Workflow w(config.id);
  ActorRegistry reg;
  std::unordered_map<std::string, std::string> actor_id_to_type;

  for (const TaskConfig& t : config.tasks) {
    w.add_phase(Phase{.id = t.id, .name = t.id, .process_ids = {t.id}, .dependency_phase_ids = {}});
    Process proc;
    proc.id = t.id;
    proc.phase_id = t.id;
    proc.estimated_duration = t.duration;
    proc.priority = 0;
    proc.deadline = t.deadline;
    w.add_process(std::move(proc));
  }

  for (const ActorConfig& a : config.actors) {
    actor_id_to_type[a.id] = a.type;
    Actor actor;
    actor.id = a.id;
    actor.capacity = a.capacity;
    actor.current_load = 0;
    for (const auto& ww : a.windows) {
      actor.availability_windows.push_back({.start = ww.start, .end = ww.end});
    }
    reg.add(std::move(actor));
  }

  WorkflowState state;
  ScheduleResult plan_result = task_orchestrator::Scheduler::plan(w, state, reg, 0);

  result.ok = plan_result.ok;

  std::unordered_map<std::string, std::vector<std::string>> task_allowed_types;
  for (const TaskConfig& t : config.tasks) {
    task_allowed_types[t.id] = t.allowed_actor_types;
  }

  for (const Assignment& a : plan_result.assignments) {
    auto it = task_allowed_types.find(a.task_id);
    const std::vector<std::string>& allowed = it != task_allowed_types.end() ? it->second : std::vector<std::string>{};
    auto at_it = actor_id_to_type.find(a.actor_id);
    std::string const atype = at_it != actor_id_to_type.end() ? at_it->second : "";
    if (actor_type_matches(atype, allowed)) {
      result.assignments.push_back(a);
    }
  }

  std::unordered_set<TaskId> assigned;
  for (const auto& a : result.assignments) {
    assigned.insert(a.task_id);
  }

  for (const TaskConfig& t : config.tasks) {
    if (!assigned.contains(t.id)) {
      result.unfulfilled_task_ids.push_back(t.id);
      result.capacity_issue = true;
    }
  }

  return result;
}

}  // namespace task_orchestrator::app
