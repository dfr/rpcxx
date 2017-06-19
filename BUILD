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
