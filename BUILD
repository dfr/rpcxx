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

test_suite(
    name = "small",
    tags = ["small"]
)

cc_test(
    name = "rpc_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["src/test/*.cpp"]),
    deps = [
        ":rpcxx",
        "//third_party/gflags",
        "//third_party/glog",
        "//third_party/gtest"
    ],
    linkstatic = 1,
    linkopts = select({
        ":freebsd": ["-lgssapi"],
        ":darwin": ["-framework GSS"],
    }),
    data = [
        "data/krb5/krb5.keytab",
        "data/krb5/krb5.conf",
        "data/krb5/run-kdc.sh",
        "data/krb5/db.dump",
    ],
    args = [
        "--keytab=$(location data/krb5/krb5.keytab)",
        "--krb5config=$(location data/krb5/krb5.conf)",
        "--runkdc=$(location data/krb5/run-kdc.sh)",
    ]
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
