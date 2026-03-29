# Task Orchestrator - Bazel workspace
workspace(name = "task_orchestrator")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# C++ rules
http_archive(
    name = "rules_cc",
    sha256 = "283fa1cdaaf172337898749cf4b9b1ef5ea269da59540954e51fba0e7b8f277a",
    strip_prefix = "rules_cc-0.2.17",
    url = "https://github.com/bazelbuild/rules_cc/releases/download/0.2.17/rules_cc-0.2.17.tar.gz",
)

load("@rules_cc//cc:extensions.bzl", "compatibility_proxy_repo")

compatibility_proxy_repo()

# Boost (headers; MSM and dependencies are header-only)
http_archive(
    name = "boost",
    build_file = "//third_party:boost.BUILD",
    sha256 = "cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454",
    strip_prefix = "boost_1_84_0",
    url = "https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.bz2",
)

http_archive(
    name = "sqlitecpp",
    build_file = "//third_party:sqlitecpp.BUILD",
    sha256 = "33bd4372d83bc43117928ee842be64d05e7807f511b5195f85d30015cad9cac6",
    strip_prefix = "SQLiteCpp-3.3.3",
    url = "https://github.com/SRombauts/SQLiteCpp/archive/refs/tags/3.3.3.tar.gz",
)
