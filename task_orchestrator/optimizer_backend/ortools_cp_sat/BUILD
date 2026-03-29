load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ortools_cp_sat_backend",
    srcs = ["ortools_cp_sat_backend.cpp"],
    tags = ["manual"],
    deps = [
        "//task_orchestrator:optimizer_api",
        "@or_tools//ortools/sat:cp_model",
    ],
    alwayslink = True,
)
