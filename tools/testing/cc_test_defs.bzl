"""Starlark helpers for defining C++ tests from globs."""

load("@rules_cc//cc:defs.bzl", "cc_test")

def cc_tests_from_glob(
        default_deps,
        src_glob = ["*_test.cpp"],
        size = "small",
        per_test_deps = {}):
    """Creates one cc_test target per matched source file."""
    for src in native.glob(src_glob):
        cc_test(
            name = src.removesuffix(".cpp"),
            size = size,
            srcs = [src],
            deps = default_deps + per_test_deps.get(src, []),
        )
