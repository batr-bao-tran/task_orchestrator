load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//tools/testing:cc_test_defs.bzl", "cc_tests_from_glob")

cc_library(
    name = "test_support_tls_material",
    testonly = True,
    srcs = ["test_support_tls_material.cpp"],
    hdrs = ["test_support_tls_material.hpp"],
    includes = ["."],
    visibility = [
        "//application/tests:__pkg__",
        "//protocol/tests:__pkg__",
    ],
    deps = [
        "@@boringssl+//:crypto",
    ],
)

cc_library(
    name = "test_support_tls_material_impl_source",
    testonly = True,
    hdrs = ["test_support_tls_material.hpp"],
    includes = ["."],
    textual_hdrs = ["test_support_tls_material.cpp"],
)

cc_tests_from_glob(
    size = "small",
    default_deps = [
        "@googletest//:gtest_main",
    ],
    linkstatic = True,
    per_test_deps = {
        "grpc_transport_impl_test.cpp": [
            "//protocol:grpc_transport_impl_source",
            "//protocol:grpc_transport_detail",
            "//protocol:runtime_api",
            "//protocol:task_orchestration_service_cc_grpc",
            "//protocol:tls_credentials",
            "//utils",
        ],
        "grpc_transport_internal_test.cpp": [
            "//protocol:grpc_transport_detail",
            "//protocol:grpc_transport",
            "//protocol:runtime_api",
            "//protocol:task_orchestration_service_cc_grpc",
            ":test_support_tls_material",
            "//protocol:tls_credentials",
            "//utils",
        ],
        "http_transport_internal_test.cpp": [
            "//protocol:http_transport_detail",
            "//protocol:http_transport",
            "//protocol:runtime_api",
            ":test_support_tls_material",
            "//protocol:tls_credentials",
            "//utils",
            "@boost//:boost",
            "@@boringssl+//:ssl",
        ],
        "tls_credentials_test.cpp": [
            "//protocol:tls_credentials",
        ],
        "test_support_tls_material_internal_test.cpp": [
            ":test_support_tls_material_impl_source",
            "@@boringssl+//:crypto",
        ],
    },
)
