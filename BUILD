cc_library(
    name = "rpcxx",
    copts = ["-std=c++14"],
    srcs = glob([
            "src/*.cpp",
            "include/rpc++/*.h"]),
    deps = ["//third_party/glog:glog"],
    includes = ["include"],
    visibility = ["//visibility:public"],
    linkopts = select({
        ":freebsd": ["-pthread", "-lgssapi", "-lm"],
        ":darwin": ["-framework GSS", "-framework CoreFoundation"],
    }),
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
    linkopts = select({
        ":freebsd": ["-lgssapi"],
        ":darwin": ["-framework GSS"],
    }),
    data = ["test.keytab"]
)

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "freebsd",
    values = {"cpu": "freebsd"},
    visibility = ["//visibility:public"],
)
