load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "solver_backends",
    tags = ["manual"],
    deps = [
        "//task_orchestrator/optimizer_backend/commercial_mip:commercial_mip_backend",
        "//task_orchestrator/optimizer_backend/ortools_cp_sat:ortools_cp_sat_backend",
    ],
    alwayslink = True,
)
