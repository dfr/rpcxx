/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <gflags/gflags.h>
#include <gtest/gtest.h>

int main(int argc, char **argv) {
    gflags::AllowCommandLineReparsing();
    gflags::ParseCommandLineFlags(&argc, &argv, false);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

