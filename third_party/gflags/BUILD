licenses(["notice"])

cc_library(
    visibility = ["//visibility:public"],
    name = "gflags",
    srcs = [
        "upstream/src/gflags.cc",
        "upstream/src/gflags_completions.cc",
        "upstream/src/gflags_reporting.cc",
    ] + glob([
        "upstream/src/*.h",
    ]),
    includes = ["gen/include"],
    copts = [
        "-Ithird_party/gflags/gen/include/gflags"
    ]
)
