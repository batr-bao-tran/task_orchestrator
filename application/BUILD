load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "config_types",
    hdrs = ["config/config.hpp"],
    includes = ["."],
    visibility = [
        "//application:__subpackages__",
        "//visibility:public",
    ],
    deps = [
        "//protocol:runtime_api",
        "//task_orchestrator",
        "//utils",
    ],
)

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
        ":config_types",
        "//task_orchestrator",
        "//utils",
        "@yaml-cpp//:yaml-cpp",
    ],
)

cc_library(
    name = "runner_types",
    hdrs = ["runner/include/runner/run_result.hpp"],
    includes = [
        ".",
        "runner/include",
    ],
    visibility = [
        "//application:__subpackages__",
        "//visibility:public",
    ],
    deps = [
        "//task_orchestrator",
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
        ":runner_types",
        "//task_orchestrator",
    ],
)

cc_library(
    name = "launcher",
    srcs = [
        "runner/src/application.cpp",
        "runner/src/detail/application_detail.hpp",
    ],
    hdrs = ["runner/include/runner/application.hpp"],
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
        ":runner",
        ":runtime_service",
        "//protocol:grpc_transport",
        "//protocol:http_transport",
        "//task_orchestrator",
        "//utils",
    ],
)

cc_library(
    name = "launcher_detail",
    hdrs = ["runner/src/detail/application_detail.hpp"],
    includes = ["."],
    visibility = ["//application:__subpackages__"],
    deps = [
        ":config",
        ":runner",
        ":runner_types",
        ":runtime_service",
        "//protocol:runtime_api",
        "//task_orchestrator",
        "//utils",
    ],
)

cc_library(
    name = "runtime_service",
    srcs = [
        "runtime_service/src/in_memory_runtime_service.cpp",
        "runtime_service/src/in_memory_runtime_service_detail.hpp",
    ],
    hdrs = ["runtime_service/include/runtime_service/in_memory_runtime_service.hpp"],
    includes = [
        ".",
        "runtime_service/include",
    ],
    visibility = [
        "//application:__subpackages__",
        "//visibility:public",
    ],
    deps = [
        ":config",
        ":runner",
        "//protocol:runtime_api",
        "//utils",
    ],
)

cc_library(
    name = "runtime_service_detail",
    hdrs = ["runtime_service/src/in_memory_runtime_service_detail.hpp"],
    includes = ["."],
    visibility = ["//application:__subpackages__"],
    deps = [
        ":config",
        ":runner_types",
        "//protocol:runtime_api",
    ],
)

cc_library(
    name = "api",
    deps = [":runtime_service"],
)

cc_library(
    name = "runner_with_optional_backends",
    tags = ["manual"],
    deps = [
        ":runner",
        "//task_orchestrator/optimizer_backend/optional:solver_backends",
    ],
)

cc_binary(
    name = "run_config",
    srcs = ["main.cpp"],
    includes = ["."],
    deps = [":launcher"],
)

cc_binary(
    name = "run_config_with_optional_backends",
    srcs = ["main.cpp"],
    includes = ["."],
    tags = ["manual"],
    deps = [
        ":launcher",
        "//protocol:grpc_transport",
        "//protocol:http_transport",
        "//task_orchestrator/optimizer_backend/optional:solver_backends",
    ],
)
