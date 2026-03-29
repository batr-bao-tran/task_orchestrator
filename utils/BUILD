load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "utils",
    srcs = [
        "src/logger.cpp",
        "src/sim_clock.cpp",
        "src/task_executor.cpp",
    ],
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "@boost",
        "@spdlog",
    ],
)
