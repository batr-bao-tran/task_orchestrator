load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "config",
    srcs = ["config/config_loader.cpp"],
    hdrs = ["config/config.hpp"],
    includes = ["."],
    visibility = [
        "//application:__subpackages__",
        "//visibility:public",
    ],
    deps = [
        "//task_orchestrator",
        "//utils",
        "@yaml-cpp//:yaml-cpp",
    ],
)

cc_library(
    name = "runner",
    srcs = ["runner/src/runner.cpp"],
    hdrs = [
        "runner/include/runner/run_result.hpp",
        "runner/include/runner/runner.hpp",
    ],
    includes = [
        ".",
        "runner/include",
    ],
    visibility = [
        "//application:__subpackages__",
        "//visibility:public",
    ],
    deps = [
        ":config",
        "//task_orchestrator",
    ],
)

cc_binary(
    name = "run_config",
    srcs = ["main.cpp"],
    includes = ["."],
    deps = [":runner"],
)
