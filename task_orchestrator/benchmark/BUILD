load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "runtime_strategy_benchmark",
    srcs = ["strategy_benchmark.cpp"],
    deps = ["//task_orchestrator"],
)

alias(
    name = "strategy_benchmark",
    actual = ":runtime_strategy_benchmark",
)

cc_binary(
    name = "optimizer_backend_benchmark",
    srcs = ["backend_benchmark.cpp"],
    tags = ["manual"],
    deps = [
        "//task_orchestrator",
        "//task_orchestrator/optimizer_backend/optional:solver_backends",
    ],
)
