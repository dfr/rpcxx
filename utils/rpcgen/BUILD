#-
# Copyright (c) 2016 Doug Rabson
# All rights reserved.
#

cc_binary(
    name = "rpcgen",
    copts = ["-std=c++14"],
    srcs = ["rpcgen.cpp"],
    deps = [":genlib"],
    linkopts = select({
        ":freebsd": ["-lm"],
        ":darwin": [],
    }),
    visibility = ["//visibility:public"]
)

cc_library(
    name = "genlib",
    copts = ["-std=c++14"],
    srcs = glob(["*.h", "*.cpp"], exclude=["rpcgen.cpp"]),
)

test_suite(
    name = "small",
    tags = ["small"],
)

cc_test(
    name = "rpcgen_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["test/*.cpp"]) + [":test_client"],
    deps = [":genlib",
            "//:rpcxx",
            "//third_party/gtest:gtest_main"],
    linkopts = select({
        ":freebsd": ["-lm"],
        ":darwin": [],
    }),
    linkstatic = 1
)

genrule(
    name = "test_client",
    srcs = ["test/test.x"],
    outs = ["test/test.h"],
    cmd = "$(location :rpcgen) -txics $(SRCS) > $(OUTS)",
    tools = [":rpcgen"]
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
