cc_binary(
    name = "rpcgen",
    copts = ["-std=c++14"],
    srcs = ["rpcgen.cpp"],
    deps = [":genlib"]
)

cc_library(
    name = "genlib",
    copts = ["-std=c++14"],
    srcs = glob(["*.h"]) + ["lexer.cpp", "parser.cpp"]
)

cc_test(
    name = "rpcgen_test",
    size = "small",
    copts = ["-std=c++14"],
    srcs = glob(["test/*.cpp"]),
    deps = [":genlib",
            "//third_party/gtest:gtest_main"]
)
