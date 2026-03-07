# Module extension to provide Boost for task_orchestrator
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _boost_impl(mctx):
    http_archive(
        name = "boost",
        url = "https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.bz2",
        strip_prefix = "boost_1_84_0",
        build_file = Label("//third_party:boost.BUILD"),
        sha256 = "cc4b893acf645c9d4b698e9a0f08ca8846aa5d6c68275c14c3e7949c24109454",
        patches = [Label("//third_party:boost-libstdcpp3-gcc13.patch")],
        patch_tool = "patch",
        patch_args = ["-p1"],
    )

boost = module_extension(
    implementation = _boost_impl,
)
