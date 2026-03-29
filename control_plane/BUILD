load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "integration",
    srcs = ["src/integration/in_memory_connector_registry.cpp"],
    hdrs = [
        "include/control_plane/integration/adapter.hpp",
        "include/control_plane/integration/connector_registry.hpp",
    ],
    includes = ["include"],
    deps = [
        "//utils",
    ],
)

cc_library(
    name = "store",
    srcs = ["src/store/sqlite_workflow_store.cpp"],
    hdrs = [
        "include/control_plane/store/sqlite_workflow_store.hpp",
        "include/control_plane/store/workflow_store.hpp",
    ],
    includes = ["include"],
    deps = [
        "//protocol:control_plane_api",
        "//protocol:runtime_api",
        "//utils",
        "@sqlitecpp",
    ],
)

cc_library(
    name = "service",
    srcs = [
        "src/service/control_plan_service.cpp",
        "src/service/event_storage_management_service.cpp",
        "src/service/plan_diff.cpp",
        "src/service/workflow_update_feed.cpp",
    ],
    hdrs = [
        "include/control_plane/service/control_plan_service.hpp",
        "include/control_plane/service/event_storage_management_service.hpp",
        "include/control_plane/service/plan_diff.hpp",
        "include/control_plane/service/workflow_update_feed.hpp",
    ],
    includes = ["include"],
    deps = [
        ":integration",
        ":store",
        "//protocol:control_plane_api",
        "//protocol:runtime_api",
        "//utils",
    ],
)

cc_library(
    name = "control_plane",
    deps = [
        ":integration",
        ":service",
        ":store",
    ],
)

cc_test(
    name = "control_plan_service_test",
    srcs = ["tests/control_plan_service_test.cpp"],
    deps = [
        ":service",
        ":store",
        "//protocol:control_plane_api",
        "//protocol:runtime_api",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "event_storage_management_service_test",
    srcs = ["tests/event_storage_management_service_test.cpp"],
    deps = [
        ":service",
        ":store",
        "//protocol:control_plane_api",
        "//protocol:runtime_api",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "integration_test",
    srcs = ["tests/integration_test.cpp"],
    deps = [
        ":integration",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sqlite_workflow_store_test",
    srcs = ["tests/sqlite_workflow_store_test.cpp"],
    deps = [
        ":store",
        "//protocol:control_plane_api",
        "//protocol:runtime_api",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "plan_diff_test",
    srcs = ["tests/plan_diff_test.cpp"],
    deps = [
        ":service",
        "//protocol:control_plane_api",
        "//protocol:runtime_api",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "workflow_update_feed_test",
    srcs = ["tests/workflow_update_feed_test.cpp"],
    deps = [
        ":service",
        "@googletest//:gtest_main",
    ],
)
