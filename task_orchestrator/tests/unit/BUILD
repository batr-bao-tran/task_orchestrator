load("@rules_cc//cc:defs.bzl", "cc_test")

_GTEST_DEPS = [
    "//task_orchestrator:task_orchestrator",
    "@googletest//:gtest_main",
]

cc_test(
    name = "types_test",
    size = "small",
    srcs = ["types_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "process_test",
    size = "small",
    srcs = ["process_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "actor_test",
    size = "small",
    srcs = ["actor_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "workflow_test",
    size = "small",
    srcs = ["workflow_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "scheduler_test",
    size = "small",
    srcs = ["scheduler_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "planner_fsm_test",
    size = "small",
    srcs = ["planner_fsm_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "orchestrator_test",
    size = "small",
    srcs = ["orchestrator_test.cpp"],
    deps = _GTEST_DEPS,
)

cc_test(
    name = "sim_clock_test",
    size = "small",
    srcs = ["sim_clock_test.cpp"],
    deps = [
        "//task_orchestrator",
        "//task_orchestrator/utils",
        "@googletest//:gtest_main",
    ],
)
