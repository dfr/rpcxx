#include <gflags/gflags.h>
#include <gtest/gtest.h>

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

