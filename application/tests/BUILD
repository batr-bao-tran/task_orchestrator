load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "config_loader_test",
    size = "small",
    srcs = ["config_loader_test.cpp"],
    deps = [
        "//application:config",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "runner_test",
    size = "small",
    srcs = ["runner_test.cpp"],
    deps = [
        "//application:runner",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "des_runner_test",
    size = "small",
    srcs = ["des_runner_test.cpp"],
    deps = [
        "//application:runner",
        "//task_orchestrator",
        "//task_orchestrator/utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "scenario_warehouse_test",
    size = "medium",
    srcs = ["scenario_warehouse_test.cpp"],
    deps = [
        "//application:config",
        "//application:runner",
        "@googletest//:gtest_main",
    ],
)
