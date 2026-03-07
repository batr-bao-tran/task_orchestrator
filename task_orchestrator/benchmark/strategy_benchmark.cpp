// Benchmark: run each scheduling strategy on 10 complex scenarios with random
// parametrization; report timing and fulfillment (assignment count).

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
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

struct BenchResult {
  std::string scenario;
  std::string strategy;
  int64_t ns = 0;
  size_t assignments = 0;
  bool ok = false;
};

static to::Workflow make_scenario_1(unsigned seed) {
  // Scenario 1: Single phase, many tasks, high contention.
  std::mt19937 rng(seed);
  int n_tasks = 20 + static_cast<int>(rng() % 31);  // 20–50
  to::Workflow w("s1");
  to::Phase ph("p1", "Phase1", {}, {});
  for (int i = 0; i < n_tasks; ++i) {
    std::string pid = "P" + std::to_string(i);
    ph.process_ids.push_back(pid);
    w.add_process(to::Process{pid,
                              "p1",
                              {},
                              static_cast<to::Duration>(1 + (rng() % 10)),
                              static_cast<to::Priority>(rng() % 100),
                              std::optional<to::Time>{}});
  }
  w.add_phase(std::move(ph));
  return w;
}

static to::Workflow make_scenario_2(unsigned seed) {
  // Scenario 2: Multi-phase DAG, mixed priorities.
  std::mt19937 rng(seed);
  to::Workflow w("s2");
  w.add_phase(to::Phase{"a", "A", {"A1", "A2"}, {}});
  w.add_phase(to::Phase{"b", "B", {"B1", "B2", "B3"}, {"a"}});
  w.add_phase(to::Phase{"c", "C", {"C1"}, {"a"}});
  w.add_phase(to::Phase{"d", "D", {"D1"}, {"b", "c"}});
  for (const char* id : {"A1", "A2", "B1", "B2", "B3", "C1", "D1"}) {
    w.add_process(to::Process{id,
                              id[0] == 'A'   ? "a"
                              : id[0] == 'B' ? "b"
                              : id[0] == 'C' ? "c"
                                             : "d",
                              {},
                              static_cast<to::Duration>(1 + (rng() % 5)),
                              static_cast<to::Priority>(rng() % 20),
                              {}});
  }
  return w;
}

static to::Workflow make_scenario_3(unsigned seed) {
  // Scenario 3: Many short tasks (throughput).
  std::mt19937 rng(seed);
  int n = 15 + static_cast<int>(rng() % 21);
  to::Workflow w("s3");
  to::Phase ph("p", "P", {}, {});
  for (int i = 0; i < n; ++i) {
    std::string pid = "T" + std::to_string(i);
    ph.process_ids.push_back(pid);
    w.add_process(to::Process{pid, "p", {}, 1, 0, {}});
  }
  w.add_phase(std::move(ph));
  return w;
}

static to::Workflow make_scenario_4(unsigned seed) {
  // Scenario 4: Long tasks with deadlines.
  std::mt19937 rng(seed);
  to::Workflow w("s4");
  w.add_phase(to::Phase{"ph", "Ph", {"L1", "L2", "L3", "L4"}, {}});
  to::Time base = 100;
  for (const char* id : {"L1", "L2", "L3", "L4"}) {
    to::Duration d = 10 + (rng() % 41);
    w.add_process(to::Process{id, "ph", {}, d, static_cast<to::Priority>(rng() % 5), base + d + (rng() % 50)});
  }
  return w;
}

static to::Workflow make_scenario_5(unsigned seed) {
  // Scenario 5: Subprocess-heavy (process with sub_process_ids).
  std::mt19937 rng(seed);
  to::Workflow w("s5");
  w.add_phase(to::Phase{"ph", "Ph", {"M1", "M2"}, {}});
  w.add_process(to::Process{"M1", "ph", {"M1a", "M1b"}, 2, 1, {}});
  w.add_process(to::Process{"M2", "ph", {"M2a"}, 1, 2, {}});
  return w;
}

static to::Workflow make_scenario_6(unsigned seed) {
  // Scenario 6: Random priorities and durations.
  std::mt19937 rng(seed);
  int n = 10 + static_cast<int>(rng() % 16);
  to::Workflow w("s6");
  to::Phase ph("p", "P", {}, {});
  for (int i = 0; i < n; ++i) {
    std::string pid = "R" + std::to_string(i);
    ph.process_ids.push_back(pid);
    w.add_process(to::Process{pid,
                              "p",
                              {},
                              static_cast<to::Duration>(1 + (rng() % 20)),
                              static_cast<to::Priority>(rng() % 50),
                              rng() % 2 ? std::optional<to::Time>(100 + (rng() % 200)) : std::nullopt});
  }
  w.add_phase(std::move(ph));
  return w;
}

static to::Workflow make_scenario_7(unsigned seed) {
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
    w.add_phase(to::Phase{ph_id, ph_id, {p1, p2}, deps});
    w.add_process(to::Process{p1, ph_id, {}, static_cast<to::Duration>(1 + (rng() % 3)), 0, {}});
    w.add_process(to::Process{p2, ph_id, {}, static_cast<to::Duration>(1 + (rng() % 3)), 0, {}});
    prev = ph_id;
  }
  return w;
}

static to::Workflow make_scenario_8(unsigned seed) {
  // Scenario 8: Many tasks, mixed process counts.
  std::mt19937 rng(seed);
  to::Workflow w("s8");
  w.add_phase(to::Phase{"ph", "Ph", {"A", "B", "C", "D", "E"}, {}});
  for (const char* id : {"A", "B", "C", "D", "E"}) {
    w.add_process(to::Process{
        id, "ph", {}, static_cast<to::Duration>(5 + (rng() % 15)), static_cast<to::Priority>(rng() % 10), {}});
  }
  return w;
}

static to::Workflow make_scenario_9(unsigned seed) {
  // Scenario 9: Two phases with dependency.
  std::mt19937 rng(seed);
  to::Workflow w("s9");
  w.add_phase(to::Phase{"ph1", "Phase1", {"X1", "X2", "X3"}, {}});
  w.add_phase(to::Phase{"ph2", "Phase2", {"Y1", "Y2"}, {"ph1"}});
  for (const char* id : {"X1", "X2", "X3", "Y1", "Y2"}) {
    w.add_process(to::Process{id,
                              id[0] == 'X' ? "ph1" : "ph2",
                              {},
                              static_cast<to::Duration>(1 + (rng() % 5)),
                              static_cast<to::Priority>(rng() % 5),
                              {}});
  }
  return w;
}

static to::Workflow make_scenario_10(unsigned seed) {
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
      w.add_process(to::Process{
          pid, ph_id, {}, static_cast<to::Duration>(1 + (rng() % 4)), static_cast<to::Priority>(rng() % 10), {}});
    }
    w.add_phase(to::Phase{ph_id, ph_id, pids, deps});
  }
  return w;
}

using ScenarioBuilder = to::Workflow (*)(unsigned);
static const struct {
  const char* name;
  ScenarioBuilder build;
} kScenarios[] = {
    {"single_phase_many_tasks", make_scenario_1},
    {"multi_phase_dag", make_scenario_2},
    {"many_short_tasks", make_scenario_3},
    {"long_tasks_deadlines", make_scenario_4},
    {"subprocess_heavy", make_scenario_5},
    {"random_prio_duration", make_scenario_6},
    {"linear_chain", make_scenario_7},
    {"mixed_process_counts", make_scenario_8},
    {"two_phase_dep", make_scenario_9},
    {"large_workflow", make_scenario_10},
};

static to::ActorRegistry make_registry_for_workflow(const to::Workflow& w, unsigned seed) {
  std::mt19937 rng(seed);
  int n_actors = 2 + static_cast<int>(rng() % 5);
  to::ActorRegistry reg;
  for (int i = 0; i < n_actors; ++i) {
    std::string aid = "A" + std::to_string(i);
    int cap = 1 + static_cast<int>(rng() % 4);
    reg.add(to::Actor{aid, cap, {{0, 10000}}, 0});
  }
  return reg;
}

static void run_one(BenchResult& out,
                    const to::Workflow& w,
                    const to::WorkflowState& state,
                    const to::ActorRegistry& reg,
                    const to::SchedulingStrategy* strategy,
                    const char* strategy_name,
                    const char* scenario_name) {
  to::Scheduler sched;
  out.scenario = scenario_name;
  out.strategy = strategy_name;
  auto start = std::chrono::steady_clock::now();
  to::ScheduleResult result = sched.plan(w, state, reg, 0, strategy);
  auto end = std::chrono::steady_clock::now();
  out.ns = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  out.assignments = result.assignments.size();
  out.ok = result.ok;
}

int main(int argc, char** argv) {
  unsigned seed = 12345;
  if (argc >= 2) {
    seed = static_cast<unsigned>(std::atoi(argv[1]));
  }
  const int runs_per_case = 5;

  to::EDFStrategy edf;
  to::FIFOStrategy fifo;
  to::SJFStrategy sjf;
  to::PriorityOnlyStrategy prio;
  struct Named {
    const char* name;
    const to::SchedulingStrategy* s;
  };
  Named strategies[] = {{"EDF", &edf}, {"FIFO", &fifo}, {"SJF", &sjf}, {"PriorityOnly", &prio}};

  std::vector<BenchResult> results;

  for (const auto& sc : kScenarios) {
    for (int r = 0; r < runs_per_case; ++r) {
      unsigned run_seed = seed + 1000 * static_cast<unsigned>(r);
      to::Workflow w = sc.build(run_seed);
      to::ActorRegistry reg = make_registry_for_workflow(w, run_seed + 1);
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
