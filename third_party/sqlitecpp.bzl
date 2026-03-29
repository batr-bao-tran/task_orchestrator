load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _sqlitecpp_impl(mctx):
    _ = mctx
    http_archive(
        name = "sqlitecpp",
        url = "https://github.com/SRombauts/SQLiteCpp/archive/refs/tags/3.3.3.tar.gz",
        strip_prefix = "SQLiteCpp-3.3.3",
        build_file = Label("//third_party:sqlitecpp.BUILD"),
        sha256 = "33bd4372d83bc43117928ee842be64d05e7807f511b5195f85d30015cad9cac6",
    )

sqlitecpp = module_extension(
    implementation = _sqlitecpp_impl,
)
