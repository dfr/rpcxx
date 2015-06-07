cc_library(
    name = "rpc",
    copts = ["-std=c++14"],
    srcs = glob([
            "src/*.cpp",
            "include/rpc++/*.h"]),
    deps = ["//third_party/glog:glog"],
    includes = ["include"],
    visibility = ["//visibility:public"]
)

cc_test(
    name = "rpc_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["src/test/*.cpp"]),
    deps = [
        ":rpc",
        "//third_party/glog:glog",
        "//third_party/gtest:gtest_main"]
)
