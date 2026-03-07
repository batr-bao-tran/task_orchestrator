load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "edf_strategy_test",
    size = "small",
    srcs = ["edf_strategy_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fifo_strategy_test",
    size = "small",
    srcs = ["fifo_strategy_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sjf_strategy_test",
    size = "small",
    srcs = ["sjf_strategy_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "priority_only_strategy_test",
    size = "small",
    srcs = ["priority_only_strategy_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)
