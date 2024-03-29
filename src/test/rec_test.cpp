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

#include <array>
#include <deque>
#include <rpc++/rec.h>
#include <gtest/gtest.h>

using namespace oncrpc;
using namespace std;
using namespace std::placeholders;

class RecTest: public ::testing::Test
{
public:
    ptrdiff_t flush(const void* buf, size_t sz)
    {
        EXPECT_GT(queue.size(), 0);
        vector<uint8_t> rec;
        rec.resize(sz);
        std::copy_n(reinterpret_cast<const uint8_t*>(buf), sz, rec.begin());
        EXPECT_EQ(rec, queue[0]);
        queue.pop_front();
        return sz;
    }

    ptrdiff_t fill(void* buf, size_t sz)
    {
        EXPECT_GT(queue.size(), 0);
        EXPECT_GE(sz, queue[0].size());
        auto q = std::move(queue[0]);
        std::copy(q.begin(), q.end(), reinterpret_cast<uint8_t*>(buf));
        queue.pop_front();
        return q.size();
    }

    unique_ptr<RecordWriter> writer(size_t bufferSize = 1500)
    {
        return make_unique<RecordWriter>(
            bufferSize, bind(&RecTest::flush, this, _1, _2));
    }

    unique_ptr<RecordReader> reader(size_t bufferSize = 1500)
    {
        return make_unique<RecordReader>(
            bufferSize, bind(&RecTest::fill, this, _1, _2));
    }

    deque<vector<uint8_t>> queue;
};

TEST_F(RecTest, SimpleWriter)
{
    // Test writing a simple record
    auto xdrs = writer();
    queue = {{128, 0, 0, 4, 0, 0, 0, 123}};
    uint32_t ui = 123;
    xdr(ui, xdrs.get());
    xdrs->pushRecord();
}

TEST_F(RecTest, WriteFragments)
{
    // Splitting across multiple fragments
    auto xdrs = writer(8);
    queue = {{0, 0, 0, 4, 1, 2, 3, 4},
             {0, 0, 0, 4, 5, 6, 7, 8},
             {0, 0, 0, 4, 9, 10, 11, 12},
             {128, 0, 0, 4, 13, 14, 15, 16}};
    array<uint8_t, 16> ub =
        {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    xdr(ub, xdrs.get());
    xdrs->pushRecord();
}

TEST_F(RecTest, SimpleRead)
{
    // Test reading a simple record
    auto xdrs = reader();
    queue = {{128, 0, 0, 4, 0, 0, 0, 123}};
    uint32_t v;
    xdr(v, xdrs.get());
    EXPECT_EQ(v, 123);
    xdrs->endRecord();
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(RecTest, ReadFragments)
{
    // Splitting across multiple fragments
    auto xdrs = reader();
    queue = {{0, 0, 0, 4, 1, 2, 3, 4},
             {0, 0, 0, 4, 5, 6, 7, 8},
             {0, 0, 0, 4, 9, 10, 11, 12},
             {128, 0, 0, 4, 13, 14, 15, 16}};
    array<uint8_t, 16> b;
    xdr(b, xdrs.get());
    array<uint8_t, 16> t{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    EXPECT_EQ(b, t);
    xdrs->endRecord();
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(RecTest, ReadUnalignedFragments)
{
    // Multiple buffered fragments, not always word aligned
    auto xdrs = reader();
    queue = {{0, 0, 0, 3, 1, 2, 3,
              0, 0, 0, 5, 4, 5, 6, 7, 8,
              0, 0, 0, 4, 9, 10, 11, 12,
              128, 0, 0, 4, 13, 14, 15, 16}};
    array<uint8_t, 16> b;
    std::fill(b.begin(), b.end(), 0);
    xdr(b, xdrs.get());
    array<uint8_t, 16> t{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    EXPECT_EQ(b, t);
    xdrs->endRecord();
    EXPECT_EQ(queue.size(), 0);

    uint32_t v1, v2;
    queue = {{0, 0, 0, 3, 1, 2, 3,
              128, 0, 0, 5, 4, 5, 6, 7, 8}};
    xdr(v1, xdrs.get());
    xdr(v2, xdrs.get());
    EXPECT_EQ(v1, 0x01020304);
    EXPECT_EQ(v2, 0x05060708);
    xdrs->endRecord();
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(RecTest, SplitHeader)
{
    // Record header split across packets
    auto xdrs = reader();
    queue = {{128, 0, 0},
             {4, 1, 2, 3, 4}};
    uint32_t v;
    xdr(v, xdrs.get());
    EXPECT_EQ(v, 0x01020304);
    xdrs->endRecord();
    EXPECT_EQ(queue.size(), 0);
}

TEST_F(RecTest, ArrayPadding)
{
    // Padding for byte arrays not a multiple of uint.sizeof
    auto xdrs = reader();
    queue = {{128, 0, 0, 4, 1, 2, 3, 0}};
    array<uint8_t, 3> c;
    xdr(c, xdrs.get());
    xdrs->endRecord();
    array<uint8_t, 3> tc{{1, 2, 3}};
    EXPECT_EQ(c, tc);

    // Test skipRecord
    queue = {{0, 0, 0, 4, 1, 2, 3, 4},
             {0, 0, 0, 4, 5, 6, 7, 8},
             {0, 0, 0, 4, 9, 10, 11, 12},
             {128, 0, 0, 4, 13, 14, 15, 16}};
    uint32_t v;
    xdr(v, xdrs.get());
    EXPECT_EQ(v, 0x01020304);
    xdrs->skipRecord();
    EXPECT_EQ(queue.size(), 0);
}
