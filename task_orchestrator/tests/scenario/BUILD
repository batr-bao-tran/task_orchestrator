load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "multi_actor_workflow_test",
    size = "medium",
    srcs = ["multi_actor_workflow_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "subprocess_workflow_test",
    size = "medium",
    srcs = ["subprocess_workflow_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "throughput_scheduling_test",
    size = "medium",
    srcs = ["throughput_scheduling_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "uptime_availability_test",
    size = "medium",
    srcs = ["uptime_availability_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fsm_lifecycle_test",
    size = "medium",
    srcs = ["fsm_lifecycle_test.cpp"],
    deps = [
        "//task_orchestrator",
        "@googletest//:gtest_main",
    ],
)
