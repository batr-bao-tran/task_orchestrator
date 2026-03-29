load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "@googletest//:gtest_main",
    ],
    per_test_deps = {
        "application_detail_test.cpp": [
            "//application:launcher",
            "//application:launcher_detail",
        ],
        "application_test.cpp": [
            "//application:launcher",
        ],
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
        "runtime_api_test.cpp": [
            "//application:runtime_service",
            "//protocol:grpc_transport",
            "//protocol:http_transport",
            "//protocol:test_support_tls_material",
            "//protocol:tls_credentials",
        ],
    },
)
