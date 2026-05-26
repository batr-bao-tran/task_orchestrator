"""Starlark helpers for defining C++ tests from globs."""

load("@rules_cc//cc:defs.bzl", "cc_test")

def cc_tests_from_glob(
        default_deps,
        src_glob = ["*_test.cpp"],
        size = "small",
        per_test_deps = {},
        per_test_data = {},
        per_test_size = {},
        linkstatic = True):
    """Creates one cc_test target per matched source file."""
    for src in native.glob(src_glob):
        cc_test(
            name = src.split("/")[-1].removesuffix(".cpp"),
            size = per_test_size.get(src, size),
            srcs = [src],
            data = per_test_data.get(src, []),
            deps = default_deps + per_test_deps.get(src, []),
            linkstatic = linkstatic,
        )
