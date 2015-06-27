cc_library(
    name = "rpcxx",
    copts = ["-std=c++14"],
    srcs = glob([
            "src/*.cpp",
            "include/rpc++/*.h"]),
    deps = ["//third_party/glog:glog"],
    includes = ["include"],
    visibility = ["//visibility:public"],
    linkopts = ["-framework GSS", "-framework CoreFoundation"],
    linkstatic = 1
)

cc_test(
    name = "rpc_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["src/test/*.cpp"]),
    deps = [
        ":rpcxx",
        "//third_party/glog:glog",
        "//third_party/gtest:gtest_main"],
    linkstatic = 1,
    linkopts = ["-framework GSS"],
    data = ["test.keytab"]
)
