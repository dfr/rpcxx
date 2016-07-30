git_repository(
    name = "gflags_repo",
    remote = "https://github.com/gflags/gflags.git",
    commit = "fe57e5af4db74ab298523f06d2c43aa895ba9f98"
)

bind(
    name = "gflags",
    actual = "@gflags_repo//:gflags"
)

new_git_repository(
    name = "glog_repo",
    remote = "https://github.com/google/glog.git",
    commit = "0472b91c5defdf90cff7292e3bf7bd86770a9a0a",
    build_file = "third_party/BUILD.glog"
)

bind(
    name = "glog",
    actual = "@glog_repo//:glog"
)

new_git_repository(
    name = "gtest_repo",
    remote = "https://github.com/google/googletest.git",
    commit = "ec44c6c1675c25b9827aacd08c02433cccde7780",
    build_file = "third_party/BUILD.gtest"
)

bind(
    name = "gtest",
    actual = "@gtest_repo//:gtest"
)

bind(
    name = "gtest_main",
    actual = "@gtest_repo//:gtest_main"
)
