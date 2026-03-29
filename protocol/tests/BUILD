load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "@googletest//:gtest_main",
    ],
    per_test_deps = {
        "grpc_transport_internal_test.cpp": [
            "//protocol:grpc_transport_detail",
            "//protocol:grpc_transport",
            "//protocol:runtime_api",
            "//protocol:task_orchestration_service_cc_grpc",
            "//protocol:test_support_tls_material",
            "//protocol:tls_credentials",
            "//utils",
        ],
        "http_transport_internal_test.cpp": [
            "//protocol:http_transport_detail",
            "//protocol:http_transport",
            "//protocol:runtime_api",
            "//protocol:test_support_tls_material",
            "//protocol:tls_credentials",
            "//utils",
            "@boost//:boost",
            "@@boringssl+//:ssl",
        ],
        "tls_credentials_test.cpp": [
            "//protocol:tls_credentials",
        ],
    },
)
