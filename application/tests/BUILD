load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "@googletest//:gtest_main",
    ],
    per_test_deps = {
        "config_loader_test.cpp": [
            "//application:config",
        ],
        "runner_test.cpp": [
            "//application:runner",
        ],
        "des_runner_test.cpp": [
            "//application:runner",
            "//task_orchestrator",
            "//utils",
        ],
        "scenario_warehouse_test.cpp": [
            "//application:config",
            "//application:runner",
        ],
    },
)
