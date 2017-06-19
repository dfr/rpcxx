#-
# Copyright (c) 2016-present Doug Rabson
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
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
            "//external:gtest_main"],
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
