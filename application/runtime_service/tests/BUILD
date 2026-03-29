load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "@googletest//:gtest_main",
    ],
    per_test_deps = {
        "in_memory_runtime_service_test.cpp": [
            "//application:config",
            "//application:runtime_service",
            "//application:runtime_service_detail",
            "//application:runner",
            "//protocol:runtime_api",
            "//utils",
        ],
    },
)
