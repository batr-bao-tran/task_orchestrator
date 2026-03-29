load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sqlite3",
    srcs = ["sqlite3/sqlite3.c"],
    hdrs = ["sqlite3/sqlite3.h"],
    includes = ["sqlite3"],
    copts = [
        "-DSQLITE_ENABLE_COLUMN_METADATA",
    ],
    linkopts = [
        "-ldl",
        "-lpthread",
    ],
)

cc_library(
    name = "sqlitecpp",
    srcs = [
        "src/Backup.cpp",
        "src/Column.cpp",
        "src/Database.cpp",
        "src/Exception.cpp",
        "src/Savepoint.cpp",
        "src/Statement.cpp",
        "src/Transaction.cpp",
    ],
    hdrs = glob(["include/SQLiteCpp/*.h"]),
    includes = [
        "include",
        "sqlite3",
    ],
    copts = [
        "-DSQLITE_ENABLE_COLUMN_METADATA",
    ],
    deps = [":sqlite3"],
)
