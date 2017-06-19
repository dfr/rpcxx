/*-
 * Copyright (c) 2016-present Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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
