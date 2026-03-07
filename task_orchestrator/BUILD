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
    hdrs = glob(["include/task_orchestrator/**/*.hpp"]),
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
