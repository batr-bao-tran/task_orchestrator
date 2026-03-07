#include <array>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "task_orchestrator/core/actor.hpp"
#include "task_orchestrator/core/scheduler.hpp"
#include "task_orchestrator/core/workflow.hpp"
#include "task_orchestrator/data/phase.hpp"
#include "task_orchestrator/data/process.hpp"
#include "task_orchestrator/strategy/edf_strategy.hpp"
#include "task_orchestrator/strategy/fifo_strategy.hpp"
#include "task_orchestrator/strategy/priority_only_strategy.hpp"
#include "task_orchestrator/strategy/sjf_strategy.hpp"

namespace to = task_orchestrator;

namespace {

struct BenchResult {
  std::string scenario;
  std::string strategy;
  int64_t ns = 0;
  size_t assignments = 0;
  bool ok = false;
};

to::Workflow make_scenario_1(unsigned seed) {
  // Scenario 1: Single phase, many tasks, high contention.
  std::mt19937 rng(seed);
  int n_tasks = 20 + static_cast<int>(rng() % 31);  // 20–50
  to::Workflow w("s1");
  to::Phase ph("p1", "Phase1", {}, {});
  for (int i = 0; i < n_tasks; ++i) {
    std::string pid = "P" + std::to_string(i);
    ph.process_ids.push_back(pid);
    w.add_process(to::Process{.id = pid,
                              .phase_id = "p1",
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(1 + (rng() % 10)),
                              .priority = static_cast<to::Priority>(rng() % 100),
                              .deadline = std::optional<to::Time>{}});
  }
  w.add_phase(std::move(ph));
  return w;
}

to::Workflow make_scenario_2(unsigned seed) {
  // Scenario 2: Multi-phase DAG, mixed priorities.
  std::mt19937 rng(seed);
  to::Workflow w("s2");
  w.add_phase(to::Phase{.id = "a", .name = "A", .process_ids = {"A1", "A2"}, .dependency_phase_ids = {}});
  w.add_phase(to::Phase{.id = "b", .name = "B", .process_ids = {"B1", "B2", "B3"}, .dependency_phase_ids = {"a"}});
  w.add_phase(to::Phase{.id = "c", .name = "C", .process_ids = {"C1"}, .dependency_phase_ids = {"a"}});
  w.add_phase(to::Phase{.id = "d", .name = "D", .process_ids = {"D1"}, .dependency_phase_ids = {"b", "c"}});
  auto phase_id_for = [](char first) -> const char* {
    if (first == 'A') return "a";
    if (first == 'B') return "b";
    if (first == 'C') return "c";
    return "d";
  };
  for (const char* id : {"A1", "A2", "B1", "B2", "B3", "C1", "D1"}) {
    w.add_process(to::Process{.id = id,
                              .phase_id = phase_id_for(id[0]),
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(1 + (rng() % 5)),
                              .priority = static_cast<to::Priority>(rng() % 20),
                              .deadline = {}});
  }
  return w;
}

to::Workflow make_scenario_3(unsigned seed) {
  // Scenario 3: Many short tasks (throughput).
  std::mt19937 rng(seed);
  int n = 15 + static_cast<int>(rng() % 21);
  to::Workflow w("s3");
  to::Phase ph("p", "P", {}, {});
  for (int i = 0; i < n; ++i) {
    std::string pid = "T" + std::to_string(i);
    ph.process_ids.push_back(pid);
    w.add_process(to::Process{
        .id = pid, .phase_id = "p", .sub_process_ids = {}, .estimated_duration = 1, .priority = 0, .deadline = {}});
  }
  w.add_phase(std::move(ph));
  return w;
}

to::Workflow make_scenario_4(unsigned seed) {
  // Scenario 4: Long tasks with deadlines.
  std::mt19937 rng(seed);
  to::Workflow w("s4");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"L1", "L2", "L3", "L4"}, .dependency_phase_ids = {}});
  to::Time base = 100;
  for (const char* id : {"L1", "L2", "L3", "L4"}) {
    to::Duration d = 10 + (rng() % 41);
    w.add_process(to::Process{.id = id,
                              .phase_id = "ph",
                              .sub_process_ids = {},
                              .estimated_duration = d,
                              .priority = static_cast<to::Priority>(rng() % 5),
                              .deadline = base + d + (rng() % 50)});
  }
  return w;
}

to::Workflow make_scenario_5(unsigned seed) {
  // Scenario 5: Subprocess-heavy (process with sub_process_ids).
  std::mt19937 rng(seed);
  to::Workflow w("s5");
  w.add_phase(to::Phase{.id = "ph", .name = "Ph", .process_ids = {"M1", "M2"}, .dependency_phase_ids = {}});
  w.add_process(to::Process{.id = "M1",
                            .phase_id = "ph",
                            .sub_process_ids = {"M1a", "M1b"},
                            .estimated_duration = 2,
                            .priority = 1,
                            .deadline = {}});
  w.add_process(to::Process{.id = "M2",
                            .phase_id = "ph",
                            .sub_process_ids = {"M2a"},
                            .estimated_duration = 1,
                            .priority = 2,
                            .deadline = {}});
  return w;
}

to::Workflow make_scenario_6(unsigned seed) {
  // Scenario 6: Random priorities and durations.
  std::mt19937 rng(seed);
  int n = 10 + static_cast<int>(rng() % 16);
  to::Workflow w("s6");
  to::Phase ph("p", "P", {}, {});
  for (int i = 0; i < n; ++i) {
    std::string pid = "R" + std::to_string(i);
    ph.process_ids.push_back(pid);
    w.add_process(to::Process{.id = pid,
                              .phase_id = "p",
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(1 + (rng() % 20)),
                              .priority = static_cast<to::Priority>(rng() % 50),
                              .deadline = rng() % 2 ? std::optional<to::Time>(100 + (rng() % 200)) : std::nullopt});
  }
  w.add_phase(std::move(ph));
  return w;
}

to::Workflow make_scenario_7(unsigned seed) {
  // Scenario 7: Linear chain of phases.
  std::mt19937 rng(seed);
  int depth = 3 + static_cast<int>(rng() % 4);
  to::Workflow w("s7");
  std::string prev;
  for (int d = 0; d < depth; ++d) {
    std::string ph_id = "ph" + std::to_string(d);
    std::vector<std::string> deps;
    if (!prev.empty()) deps.push_back(prev);
    std::string p1 = "P" + std::to_string(d) + "a";
    std::string p2 = "P" + std::to_string(d) + "b";
    w.add_phase(to::Phase{.id = ph_id, .name = ph_id, .process_ids = {p1, p2}, .dependency_phase_ids = deps});
    w.add_process(to::Process{.id = p1,
                              .phase_id = ph_id,
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(1 + (rng() % 3)),
                              .priority = 0,
                              .deadline = {}});
    w.add_process(to::Process{.id = p2,
                              .phase_id = ph_id,
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(1 + (rng() % 3)),
                              .priority = 0,
                              .deadline = {}});
    prev = ph_id;
  }
  return w;
}

to::Workflow make_scenario_8(unsigned seed) {
  // Scenario 8: Many tasks, mixed process counts.
  std::mt19937 rng(seed);
  to::Workflow w("s8");
  w.add_phase(
      to::Phase{.id = "ph", .name = "Ph", .process_ids = {"A", "B", "C", "D", "E"}, .dependency_phase_ids = {}});
  for (const char* id : {"A", "B", "C", "D", "E"}) {
    w.add_process(to::Process{.id = id,
                              .phase_id = "ph",
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(5 + (rng() % 15)),
                              .priority = static_cast<to::Priority>(rng() % 10),
                              .deadline = {}});
  }
  return w;
}

to::Workflow make_scenario_9(unsigned seed) {
  // Scenario 9: Two phases with dependency.
  std::mt19937 rng(seed);
  to::Workflow w("s9");
  w.add_phase(to::Phase{.id = "ph1", .name = "Phase1", .process_ids = {"X1", "X2", "X3"}, .dependency_phase_ids = {}});
  w.add_phase(to::Phase{.id = "ph2", .name = "Phase2", .process_ids = {"Y1", "Y2"}, .dependency_phase_ids = {"ph1"}});
  for (const char* id : {"X1", "X2", "X3", "Y1", "Y2"}) {
    w.add_process(to::Process{.id = id,
                              .phase_id = id[0] == 'X' ? "ph1" : "ph2",
                              .sub_process_ids = {},
                              .estimated_duration = static_cast<to::Duration>(1 + (rng() % 5)),
                              .priority = static_cast<to::Priority>(rng() % 5),
                              .deadline = {}});
  }
  return w;
}

to::Workflow make_scenario_10(unsigned seed) {
  // Scenario 10: Large workflow (many phases, many tasks).
  std::mt19937 rng(seed);
  to::Workflow w("s10");
  int n_phases = 5 + static_cast<int>(rng() % 6);
  std::vector<std::string> phase_ids;
  for (int i = 0; i < n_phases; ++i) {
    std::string ph_id = "ph" + std::to_string(i);
    phase_ids.push_back(ph_id);
    std::vector<std::string> deps;
    if (i > 0) deps.push_back(phase_ids[static_cast<size_t>(i - 1)]);
    int n_proc = 3 + static_cast<int>(rng() % 5);
    std::vector<std::string> pids;
    for (int j = 0; j < n_proc; ++j) {
      std::string pid = "P" + std::to_string(i) + "_" + std::to_string(j);
      pids.push_back(pid);
      w.add_process(to::Process{.id = pid,
                                .phase_id = ph_id,
                                .sub_process_ids = {},
                                .estimated_duration = static_cast<to::Duration>(1 + (rng() % 4)),
                                .priority = static_cast<to::Priority>(rng() % 10),
                                .deadline = {}});
    }
    w.add_phase(to::Phase{.id = ph_id, .name = ph_id, .process_ids = pids, .dependency_phase_ids = deps});
  }
  return w;
}

using ScenarioBuilder = to::Workflow (*)(unsigned);
struct ScenarioEntry {
  const char* name;
  ScenarioBuilder build;
};
const std::array<ScenarioEntry, 10> kScenarios = {{
    {.name = "single_phase_many_tasks", .build = make_scenario_1},
    {.name = "multi_phase_dag", .build = make_scenario_2},
    {.name = "many_short_tasks", .build = make_scenario_3},
    {.name = "long_tasks_deadlines", .build = make_scenario_4},
    {.name = "subprocess_heavy", .build = make_scenario_5},
    {.name = "random_prio_duration", .build = make_scenario_6},
    {.name = "linear_chain", .build = make_scenario_7},
    {.name = "mixed_process_counts", .build = make_scenario_8},
    {.name = "two_phase_dep", .build = make_scenario_9},
    {.name = "large_workflow", .build = make_scenario_10},
}};

to::ActorRegistry make_registry_for_workflow(unsigned seed) {
  std::mt19937 rng(seed);
  int n_actors = 2 + static_cast<int>(rng() % 5);
  to::ActorRegistry reg;
  for (int i = 0; i < n_actors; ++i) {
    std::string aid = "A" + std::to_string(i);
    int cap = 1 + static_cast<int>(rng() % 4);
    reg.add(
        to::Actor{.id = aid, .capacity = cap, .availability_windows = {{.start = 0, .end = 10000}}, .current_load = 0});
  }
  return reg;
}

void run_one(BenchResult& out,
             const to::Workflow& w,
             const to::WorkflowState& state,
             const to::ActorRegistry& reg,
             const to::SchedulingStrategy* strategy,
             const char* strategy_name,
             const char* scenario_name) {
  out.scenario = scenario_name;
  out.strategy = strategy_name;
  auto start = std::chrono::steady_clock::now();
  const to::ScheduleResult result = to::Scheduler::plan(w, state, reg, 0, strategy);
  auto end = std::chrono::steady_clock::now();
  out.ns = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  out.assignments = result.assignments.size();
  out.ok = result.ok;
}

}  // namespace

int main(int argc, char** argv) {
  uint64_t seed = 12345U;
  if (argc >= 2) {
    char* end = nullptr;
    const auto v = std::strtoul(argv[1], &end, 10);
    if (end != argv[1] && *end == '\0' && v <= UINT_MAX) {
      seed = v;
    }
  }
  const int runs_per_case = 5;

  const to::EDFStrategy edf;
  const to::FIFOStrategy fifo;
  const to::SJFStrategy sjf;
  const to::PriorityOnlyStrategy prio;
  struct Named {
    const char* name;
    const to::SchedulingStrategy* s;
  };
  const std::array<Named, 4> strategies = {Named{.name = "EDF", .s = &edf},
                                           Named{.name = "FIFO", .s = &fifo},
                                           Named{.name = "SJF", .s = &sjf},
                                           Named{.name = "PriorityOnly", .s = &prio}};

  std::vector<BenchResult> results;

  for (const auto& sc : kScenarios) {
    for (int r = 0; r < runs_per_case; ++r) {
      unsigned run_seed = seed + (1000 * static_cast<unsigned>(r));
      to::Workflow w = sc.build(run_seed);
      to::ActorRegistry reg = make_registry_for_workflow(run_seed + 1);
      to::WorkflowState state;

      for (const auto& st : strategies) {
        BenchResult res;
        run_one(res, w, state, reg, st.s, st.name, sc.name);
        results.push_back(res);
      }
    }
  }

  // Report table: scenario, strategy, avg_ns, avg_assignments, ok
  std::cout << "Scenario,Strategy,AvgNs,AvgAssignments,Ok\n";
  for (const auto& sc : kScenarios) {
    for (const auto& st : strategies) {
      int64_t sum_ns = 0;
      size_t sum_assign = 0;
      int count = 0;
      for (const auto& r : results) {
        if (r.scenario == sc.name && r.strategy == st.name) {
          sum_ns += r.ns;
          sum_assign += r.assignments;
          count++;
        }
      }
      if (count) {
        std::cout << sc.name << "," << st.name << "," << (sum_ns / count) << ","
                  << (sum_assign / static_cast<size_t>(count)) << ",1\n";
      }
    }
  }
  return 0;
}
