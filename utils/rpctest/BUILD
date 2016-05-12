cc_binary(
    name = "rpctest",
    copts = ["-std=c++14"],
    srcs = ["rpctest.cpp"],
    deps = ["//:rpcxx"],
    linkopts = select({
        "//:freebsd": ["-pthread", "-lgssapi", "-lm"],
        "//:darwin": ["-framework GSS", "-framework CoreFoundation"],
    }),
)
