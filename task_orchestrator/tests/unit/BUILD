load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "//task_orchestrator:task_orchestrator",
        "@googletest//:gtest_main",
    ],
    per_test_deps = {
        "sim_clock_test.cpp": [
            "//utils",
        ],
    },
)
