cc_binary(
    name = "rpcgen",
    copts = ["-std=c++14"],
    srcs = ["rpcgen.cpp"],
    deps = [":genlib"]
)

cc_library(
    name = "genlib",
    copts = ["-std=c++14"],
    srcs = glob(["*.h", "*.cpp"], exclude=["rpcgen.cpp"])
)

cc_test(
    name = "rpcgen_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["test/*.cpp"]) + [":test_client"],
    deps = [":genlib",
            "//:rpcxx",
            "//third_party/gtest:gtest_main"],
    linkstatic = 1
)

genrule(
    name = "test_client",
    srcs = ["test/test.x"],
    outs = ["test/test.h"],
    cmd = "rpcgen -txics $(SRCS) > $(OUTS)",
    tools = [":rpcgen"]
)
