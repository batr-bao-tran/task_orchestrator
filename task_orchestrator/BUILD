load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "optimizer_api",
    srcs = [
        "src/optimizer/backend_registry.cpp",
        "src/optimizer/constraint_index.cpp",
        "src/optimizer/discrete_time_formulation.cpp",
        "src/optimizer/nlp_parser.cpp",
    ],
    hdrs = glob(["include/task_orchestrator/optimizer/**/*.hpp"]),
    includes = ["include"],
    deps = [
        "//utils",
    ],
)

cc_library(
    name = "workflow_optimizer",
    srcs = [
        "src/optimizer/workflow_optimizer.cpp",
    ],
    copts = [],
    includes = ["include"],
    deps = [
        ":optimizer_api",
        "//utils",
    ],
)

cc_library(
    name = "task_orchestrator_runtime",
    srcs = [
        "src/core/actor.cpp",
        "src/core/orchestrator.cpp",
        "src/core/planner_fsm.cpp",
        "src/core/scheduler.cpp",
        "src/core/workflow.cpp",
        "src/data/process.cpp",
    ],
    hdrs = glob([
        "include/task_orchestrator/core/**/*.hpp",
        "include/task_orchestrator/data/**/*.hpp",
        "include/task_orchestrator/strategy/**/*.hpp",
    ]),
    copts = [
        "-DBOOST_USER_CONFIG=<boost/config/user.hpp>",
        "-DBOOST_MPL_CFG_COMPILER_DIR=gcc",
    ],
    includes = ["include"],
    deps = [
        "//utils",
        "@boost",
    ],
)

cc_library(
    name = "task_orchestrator",
    hdrs = glob(["include/task_orchestrator/**/*.hpp"]),
    includes = ["include"],
    deps = [
        ":optimizer_api",
        ":task_orchestrator_runtime",
        ":workflow_optimizer",
        "//task_orchestrator/optimizer_backend/indexed_exact:indexed_exact_backend",
    ],
)
