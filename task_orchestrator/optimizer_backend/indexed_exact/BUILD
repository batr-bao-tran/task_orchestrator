load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "indexed_exact_backend",
    srcs = ["indexed_exact_backend.cpp"],
    deps = [
        "//task_orchestrator:optimizer_api",
    ],
    alwayslink = True,
)
