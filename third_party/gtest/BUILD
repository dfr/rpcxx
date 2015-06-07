licenses(["notice"])
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "gtest",
    testonly = 1,
    linkstatic = 1,
    copts = ["-Ithird_party/gtest/upstream"],
    srcs = glob([
            "upstream/src/gtest-death-test.cc",
            "upstream/src/gtest-filepath.cc",
            "upstream/src/gtest-port.cc",
            "upstream/src/gtest-printers.cc",
            "upstream/src/gtest-test-part.cc",
            "upstream/src/gtest-typed-test.cc",
            "upstream/src/gtest.cc",
            "upstream/src/gtest-internal-inl.h",
            "upstream/include/gtest/*.h"],
        exclude=["upstream/src/gtest-all.cc", "upstream/src/gtest_main.cc"]),
    hdrs = glob(["upstream/include/gtest/*.h"]),
    includes = [ "upstream/include" ],
)

cc_library(
    name = "gtest_main",
    testonly = 1,
    linkstatic = 1,
    deps = [":gtest"],
    srcs = ["upstream/src/gtest_main.cc"]
)
