load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_proto_grpc_cpp//:defs.bzl", "cpp_grpc_library", "cpp_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "task_orchestration_proto",
    srcs = ["proto/task_orchestration.proto"],
    import_prefix = "task_orchestration_service",
    strip_import_prefix = "proto",
)

proto_library(
    name = "task_orchestration_service_proto",
    srcs = ["proto/task_orchestration_service.proto"],
    import_prefix = "task_orchestration_service",
    strip_import_prefix = "proto",
    deps = [":task_orchestration_proto"],
)

proto_library(
    name = "control_plane_proto",
    srcs = ["proto/control_plane.proto"],
    import_prefix = "task_orchestration_service",
    strip_import_prefix = "proto",
    deps = [":task_orchestration_proto"],
)

proto_library(
    name = "control_plane_service_proto",
    srcs = ["proto/control_plane_service.proto"],
    import_prefix = "task_orchestration_service",
    strip_import_prefix = "proto",
    deps = [":control_plane_proto"],
)

proto_library(
    name = "operator_api_proto",
    srcs = ["proto/operator_api.proto"],
    import_prefix = "task_orchestration_service",
    strip_import_prefix = "proto",
    deps = [
        ":control_plane_proto",
        ":task_orchestration_proto",
    ],
)

proto_library(
    name = "operator_api_service_proto",
    srcs = ["proto/operator_api_service.proto"],
    import_prefix = "task_orchestration_service",
    strip_import_prefix = "proto",
    deps = [
        ":control_plane_proto",
        ":operator_api_proto",
    ],
)

cpp_proto_library(
    name = "task_orchestration_cc_proto",
    protos = [":task_orchestration_proto"],
)

cpp_grpc_library(
    name = "task_orchestration_service_cc_grpc",
    protos = [":task_orchestration_service_proto"],
    deps = [":task_orchestration_cc_proto"],
)

cpp_proto_library(
    name = "control_plane_cc_proto",
    protos = [":control_plane_proto"],
    deps = [":task_orchestration_cc_proto"],
)

cpp_grpc_library(
    name = "control_plane_service_cc_grpc",
    protos = [":control_plane_service_proto"],
    deps = [":control_plane_cc_proto"],
)

cpp_proto_library(
    name = "operator_api_cc_proto",
    protos = [":operator_api_proto"],
    deps = [
        ":control_plane_cc_proto",
        ":task_orchestration_cc_proto",
    ],
)

cpp_grpc_library(
    name = "operator_api_service_cc_grpc",
    protos = [":operator_api_service_proto"],
    deps = [":operator_api_cc_proto"],
)

cc_library(
    name = "runtime_api",
    hdrs = ["include/protocol/runtime_api.hpp"],
    includes = ["include"],
    deps = [
        ":task_orchestration_cc_proto",
        "//utils",
    ],
)

cc_library(
    name = "control_plane_api",
    hdrs = ["include/protocol/control_plane_api.hpp"],
    includes = ["include"],
    deps = [
        ":control_plane_cc_proto",
        ":runtime_api",
        "//utils",
    ],
)

cc_library(
    name = "operator_api",
    hdrs = ["include/protocol/operator_api.hpp"],
    includes = ["include"],
    deps = [
        ":operator_api_cc_proto",
        ":runtime_api",
        "//utils",
    ],
)

cc_library(
    name = "tls_credentials",
    srcs = ["src/tls_credentials.cpp"],
    hdrs = ["include/protocol/tls_credentials.hpp"],
    includes = ["include"],
    deps = [
        ":runtime_api",
    ],
)

cc_library(
    name = "http_transport_detail",
    hdrs = ["src/detail/http_transport_detail.hpp"],
    includes = ["."],
    deps = [
        ":http_transport",
    ],
)

cc_library(
    name = "http_transport",
    srcs = [
        "src/detail/http_transport_detail.hpp",
        "src/http_transport.cpp",
    ],
    hdrs = ["include/protocol/http_transport.hpp"],
    includes = [
        ".",
        "include",
    ],
    deps = [
        ":operator_api",
        ":runtime_api",
        ":tls_credentials",
        "//utils",
        "@@boringssl+//:ssl",
        "@boost",
        "@protobuf",
    ],
)

cc_library(
    name = "grpc_transport_detail",
    hdrs = ["src/detail/grpc_transport_detail.hpp"],
    includes = ["."],
    deps = [
        ":grpc_transport",
    ],
)

cc_library(
    name = "grpc_transport_impl_source",
    testonly = True,
    includes = ["."],
    textual_hdrs = ["src/grpc_transport.cpp"],
    visibility = ["//protocol/tests:__pkg__"],
)

cc_library(
    name = "grpc_transport",
    srcs = [
        "src/detail/grpc_transport_detail.hpp",
        "src/grpc_transport.cpp",
    ],
    hdrs = ["include/protocol/grpc_transport.hpp"],
    includes = [
        ".",
        "include",
    ],
    deps = [
        ":runtime_api",
        ":task_orchestration_service_cc_grpc",
        ":tls_credentials",
        "//utils",
    ],
)

cc_library(
    name = "transport",
    deps = [
        ":grpc_transport",
        ":http_transport",
    ],
)

filegroup(
    name = "runtime_api_proto",
    srcs = [
        "proto/control_plane.proto",
        "proto/control_plane_service.proto",
        "proto/operator_api.proto",
        "proto/operator_api_service.proto",
        "proto/task_orchestration.proto",
        "proto/task_orchestration_service.proto",
    ],
)
