/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <rpc++/timeout.h>
#include <gtest/gtest.h>

using namespace oncrpc;
using namespace std;
using namespace std::literals::chrono_literals;

namespace {

TEST(TimeoutTest, Simple)
{
    auto now = std::chrono::system_clock::now();
    bool t1 = false;
    bool t2 = false;

    TimeoutManager tman;
    tman.add(now + 2s, [&]() { t2 = true; });
    tman.add(now + 1s, [&]() { t1 = true; });

    tman.update(now + 1s);
    EXPECT_EQ(true, t1);
    EXPECT_EQ(false, t2);

    tman.update(now + 2s);
    EXPECT_EQ(true, t1);
    EXPECT_EQ(true, t2);
}

TEST(TimeoutTest, CancelActive)
{
    auto now = std::chrono::system_clock::now();
    bool t1 = false;

    TimeoutManager tman;
    auto tid = tman.add(now + 5s, [&]() { t1 = true; });
    tman.update(now + 1s);
    EXPECT_EQ(false, t1);
    tman.cancel(tid);
    tman.update(now + 10s);
    EXPECT_EQ(false, t1);
}

TEST(TimeoutTest, CancelExpired)
{
    auto now = std::chrono::system_clock::now();
    bool t1 = false;

    TimeoutManager tman;
    auto tid = tman.add(now + 5s, [&]() { t1 = true; });
    tman.update(now + 10s);
    EXPECT_EQ(true, t1);
    tman.cancel(tid);
}

}
