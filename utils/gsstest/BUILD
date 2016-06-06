#-
# Copyright (c) 2016 Doug Rabson
# All rights reserved.
#

cc_binary(
    name = "gsstest",
    srcs = ["gsstest.c"],
    #linkopts = ["-lgssapi_krb5"]
    linkopts = select({
        "//:freebsd": ["-lgssapi"],
        "//:darwin": ["-framework GSS", "-framework CoreFoundation"]
    })
)
