load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "task_orchestrator",
    srcs = [
        "src/core/actor.cpp",
        "src/core/orchestrator.cpp",
        "src/core/planner_fsm.cpp",
        "src/core/scheduler.cpp",
        "src/core/workflow.cpp",
        "src/data/process.cpp",
    ],
    hdrs = [
        "include/task_orchestrator/core/actor.hpp",
        "include/task_orchestrator/core/concepts.hpp",
        "include/task_orchestrator/core/orchestrator.hpp",
        "include/task_orchestrator/core/planner_fsm.hpp",
        "include/task_orchestrator/core/scheduler.hpp",
        "include/task_orchestrator/core/workflow.hpp",
        "include/task_orchestrator/data/phase.hpp",
        "include/task_orchestrator/data/process.hpp",
        "include/task_orchestrator/data/types.hpp",
        "include/task_orchestrator/strategy/edf_strategy.hpp",
        "include/task_orchestrator/strategy/fifo_strategy.hpp",
        "include/task_orchestrator/strategy/priority_only_strategy.hpp",
        "include/task_orchestrator/strategy/scheduling_strategy.hpp",
        "include/task_orchestrator/strategy/sjf_strategy.hpp",
    ],
    includes = ["include"],
    deps = [
        "//task_orchestrator/utils:generator",
        "@boost",
    ],
)
