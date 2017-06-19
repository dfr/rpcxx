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
#include <type_traits>
#include <vector>
#include <rpc++/xdr.h>
#include <gtest/gtest.h>

using namespace std;
using namespace oncrpc;

namespace {

class XdrTest: public ::testing::Test
{
public:
    template <typename T>
    void compare(const T& a, const T& b)
    {
        EXPECT_EQ(a, b);
    }

    template <typename T>
    void compare(const unique_ptr<T>& a, const unique_ptr<T>& b)
    {
        if (a.get() == nullptr) {
            EXPECT_EQ(a.get(), b.get());
        }
        else {
            EXPECT_EQ(*a.get(), *b.get());
        }
    }

    template <typename T, size_t N>
    void test(const T& a, array<uint8_t, N> x)
    {
        uint8_t buf[512];
        fill_n(buf, sizeof buf, 99);
        auto xdrmem = make_unique<XdrMemory>(buf, sizeof buf);
        xdr(a, static_cast<XdrSink*>(xdrmem.get()));
        EXPECT_EQ(xdrmem->writePos(), N);

        xdrmem->rewind();
        T b;
        xdr(b, static_cast<XdrSource*>(xdrmem.get()));
        compare(a, b);
        EXPECT_EQ(size_t(xdrmem->readPos()), N);
        for (size_t i = 0; i < N; i++)
            EXPECT_EQ(buf[i], x[i]);
    }
};

struct A {
    int a, b;
};

bool operator==(const A& x, const A& y)
{
    return x.a == y.a && x.b == y.b;
}

template <typename XDR>
void xdr(RefType<A, XDR> v, XDR* xdrs)
{
    xdr(v.a, xdrs);
    xdr(v.b, xdrs);
}

TEST_F(XdrTest, BasicTypes)
{
    test<int, 4>(int(0x11223344), {{17, 34, 51, 68}});
    test<unsigned int, 4>(int(0x11223344), {{17, 34, 51, 68}});
    test<unsigned long, 8>(0x0102030411223344L, {{1, 2, 3, 4, 17, 34, 51, 68}});
    test<long, 8>(0x0102030411223344L, {{1, 2, 3, 4, 17, 34, 51, 68}});
    test<float, 4>(12345678.0, {{75, 60, 97, 78}});
    test<double, 8>(12345678.0, {{65, 103, 140, 41, 192, 0, 0, 0}});
}

TEST_F(XdrTest, ByteArrays)
{
    test<array<uint8_t, 3>, 4>({{1, 2, 3}}, {{1, 2, 3, 0}});
    test<vector<uint8_t>, 8>({1, 2, 3}, {{0, 0, 0, 3, 1, 2, 3, 0}});
    test<string, 12>("hello", {{0, 0, 0, 5, 104, 101, 108, 108, 111, 0, 0, 0}});
}

TEST_F(XdrTest, PODArrays)
{
    test<array<A, 2>, 16>({{A{1, 2}, A{3, 4}}},
        {{0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4}});
    test<vector<A>, 20>({{A{1, 2}, A{3, 4}}},
        {{0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4}});
}

TEST_F(XdrTest, Pointers)
{
    unique_ptr<int> up(nullptr);
    test<unique_ptr<int>, 4>(up, {{0, 0, 0, 0}});
    up.reset(new int(4));
    test<unique_ptr<int>, 8>(up, {{0, 0, 0, 1, 0, 0, 0, 4}});
}

TEST_F(XdrTest, BoundedByteArray)
{
    auto xdrs = make_unique<XdrMemory>(512);
    vector<uint8_t> a(10, 99);
    xdr(a, static_cast<XdrSink*>(xdrs.get()));
    xdrs->rewind();

    bounded_vector<uint8_t, 5> b;
    EXPECT_THROW(xdr(b, static_cast<XdrSource*>(xdrs.get())), XdrError);
}

TEST_F(XdrTest, BoundedArray)
{
    auto xdrs = make_unique<XdrMemory>(512);
    vector<int> a(10, 99);
    xdr(a, static_cast<XdrSink*>(xdrs.get()));
    xdrs->rewind();

    bounded_vector<int, 5> b;
    EXPECT_THROW(xdr(b, static_cast<XdrSource*>(xdrs.get())), XdrError);
}

TEST_F(XdrTest, BoundedString)
{
    auto xdrs = make_unique<XdrMemory>(512);
    string a(10, 99);
    xdr(a, static_cast<XdrSink*>(xdrs.get()));
    xdrs->rewind();

    bounded_string<5> b;
    EXPECT_THROW(xdr(b, static_cast<XdrSource*>(xdrs.get())), XdrError);
}

TEST_F(XdrTest, Sizeof)
{
    EXPECT_EQ(4, XdrSizeof(42));
    EXPECT_EQ(16, XdrSizeof(array<int, 4>{{1, 2, 3, 4}}));
    EXPECT_EQ(20, XdrSizeof(vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(8, XdrSizeof(array<uint8_t, 7>{{0}}));
    EXPECT_EQ(12, XdrSizeof(string("Hello")));
}

TEST_F(XdrTest, Inline)
{
    auto xdrs = make_unique<XdrMemory>(100);
    EXPECT_NE(nullptr, xdrs->writeInline<char>(100));
    EXPECT_EQ(100, xdrs->writePos());
    EXPECT_NE(nullptr, xdrs->readInline<char>(100));
    EXPECT_EQ(100, xdrs->readPos());
}

}
