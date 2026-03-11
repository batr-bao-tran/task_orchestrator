load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
    per_test_deps = {
        "multiphase_des_ordering_param_test.cpp": [
            "//utils",
        ],
        "replanning_event_ordering_test.cpp": [
            "//utils",
        ],
    },
)
