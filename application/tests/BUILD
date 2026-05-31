load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "@googletest//:gtest_main",
    ],
    per_test_data = {
        "application_test.cpp": [
            "//application/examples:all_examples",
        ],
        "config_loader_test.cpp": [
            "//application/examples:application_config_examples",
        ],
        "scenario_warehouse_test.cpp": [
            "//application/examples:workflow_config_examples",
        ],
    },
    per_test_deps = {
        "application_detail_test.cpp": [
            "//application:launcher",
            "//application:launcher_detail",
            "//application:runtime_service",
        ],
        "application_test.cpp": [
            "//application:launcher",
        ],
        "config_loader_test.cpp": [
            "//application:config",
        ],
        "operator_service_test.cpp": [
            "//application:operator_service",
            "//control_plane",
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
            "//protocol/tests:test_support_tls_material",
            "//protocol:tls_credentials",
        ],
    },
)
