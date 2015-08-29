licenses(["notice"])

cc_library(
  visibility = ["//visibility:public"],
  name = "glog",
  deps = [
    #"//third_party/libunwind:libunwind-k8",
    "//third_party/gflags:gflags",
  ],
  includes = [
    "gen/include",
    "gen/src",
    "upstream/src",
  ],
  copts = [
    #"-DNO_FRAME_POINTER",
    "-D_GNU_SOURCE",
    #"-fno-sanitize=thread",
    #"-fno-sanitize=address",
    "-Ithird_party/glog/gen/src",
  ],
  srcs = [
    "upstream/src/demangle.cc",
    "upstream/src/logging.cc",
    "upstream/src/raw_logging.cc",
    "upstream/src/signalhandler.cc",
    "upstream/src/symbolize.cc",
    "upstream/src/utilities.cc",
    "upstream/src/vlog_is_on.cc",
  ],
  hdrs = [
    "upstream/src/demangle.h",
    "upstream/src/mock-log.h",
    "upstream/src/symbolize.h",
    "upstream/src/utilities.h",
  ],
  linkopts = [
    #"-pthread"
  ],
)

cc_test(
  name = "symbolize_unittest",
  deps = [
    ":glog",
  ],
  srcs = [
    "upstream/src/symbolize_unittest.cc",
  ],
  copts = [
    "-Ithird_party/glog/gen/src"
  ],
)

