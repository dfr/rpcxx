#-
# Copyright (c) 2016 Doug Rabson
# All rights reserved.
#

cc_library(
    name = "rpcxx",
    copts = ["-std=c++14"],
    srcs = glob(["src/*.cpp"]),
    deps = [
        ":hdrs",
        "//external:glog"
    ],
    visibility = ["//visibility:public"],
    linkopts = select({
        ":freebsd": ["-pthread", "-lgssapi", "-lm"],
        ":darwin": ["-framework GSS", "-framework CoreFoundation"],
    }),
    includes = ["include"],
    linkstatic = 1
)

cc_inc_library(
    name = "hdrs",
    hdrs = glob([ "include/rpc++/*.h"]),
)

test_suite(
    name = "small",
    tags = ["small"],
    tests = [
        ":rpc_test",
        "//utils/rpcgen:small",
    ]
)

cc_test(
    name = "rpc_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["src/test/*.cpp"]),
    deps = [
        ":rpcxx",
        "//external:gflags",
        "//external:glog",
        "//external:gtest"
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
