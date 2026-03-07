# Boost header-only usage (MSM and deps are headers).
# Archive root after strip_prefix: boost/, libs/, etc.
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "boost",
    includes = ["."],
    hdrs = glob(["boost/**/*.hpp", "boost/**/*.h", "boost/**/*.ipp"]),
    visibility = ["//visibility:public"],
)
