load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "commercial_mip_backend",
    srcs = ["commercial_mip_backend.cpp"],
    tags = ["manual"],
    deps = [
        "//task_orchestrator:optimizer_api",
        "//utils",
        "@or_tools//ortools/linear_solver",
    ],
    alwayslink = True,
)
