load("@rules_cc//cc:defs.bzl", "cc_library")

# Header-only; no task_orchestrator dependency (breaks cycle with main lib).
cc_library(
    name = "generator",
    hdrs = ["include/task_orchestrator/utils/generator.hpp"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "utils",
    srcs = ["src/sim_clock.cpp"],
    hdrs = ["include/task_orchestrator/utils/sim_clock.hpp"],
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = ["//task_orchestrator"],
)
