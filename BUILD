cc_library(
    name = "rpcxx",
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
        ":rpcxx",
        "//third_party/glog:glog",
        "//third_party/gtest:gtest_main"],
    #linkopts = ["-lgssapi_krb5"]
    linkopts = ["-framework GSS"],
    data = ["test.keytab"]
)
