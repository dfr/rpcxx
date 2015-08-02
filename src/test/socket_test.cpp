#include <array>

#include <rpc++/socket.h>
#include <gtest/gtest.h>

using namespace oncrpc;
using namespace std;
using namespace std::placeholders;

TEST(SocketTest, AddressNull)
{
    Address addr;
    EXPECT_EQ(false, addr);
}

TEST(SocketTest, AddressPath)
{
    Address addr("unix:///foo/bar");
    auto p = reinterpret_cast<const sockaddr_un*>(addr.addr());
    EXPECT_EQ(AF_LOCAL, p->sun_family);
    EXPECT_EQ(SUN_LEN(p), p->sun_len);
    EXPECT_EQ(string("/foo/bar"), string(p->sun_path));
}

TEST(SocketTest, AddressHostIPv4)
{
    struct test {
        string url;
        int port;
        array<uint8_t, 4> addr;
    };
    vector<test> tests = {
        {"tcp://127.0.0.1:1234", 1234, {{127,0,0,1}}},
        {"tcp://127.0.0.1",      0,    {{127,0,0,1}}},
    };
    for (auto t: tests) {
        Address addr(t.url);
        auto p = reinterpret_cast<const sockaddr_in*>(addr.addr());
        EXPECT_EQ(AF_INET, p->sin_family);
        EXPECT_EQ(sizeof(sockaddr_in), p->sin_len);
        EXPECT_EQ(t.port, ntohs(p->sin_port));
        EXPECT_EQ(0, memcmp(t.addr.begin(), &p->sin_addr.s_addr, 4));
    }
}

TEST(SocketTest, AddressHostIPv6)
{
    struct test {
        string url;
        int port;
        array<uint8_t, 16> addr;
    };
    vector<test> tests = {
        {"tcp://[::1]:1234", 1234,
         {{0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1}} },
        {"tcp://[1234::1]:1234", 1234,
         {{0x12,0x34,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1}} },
        {"tcp://[1234::1.2.3.4]:1234", 1234,
         {{0x12,0x34,0,0, 0,0,0,0, 0,0,0,0, 1,2,3,4}} },
    };

    for (auto t: tests) {
        Address addr(t.url);
        auto p = reinterpret_cast<const sockaddr_in6*>(addr.addr());
        EXPECT_EQ(AF_INET6, p->sin6_family);
        EXPECT_EQ(sizeof(sockaddr_in6), p->sin6_len);
        EXPECT_EQ(t.port, ntohs(p->sin6_port));
        EXPECT_EQ(0, memcmp(t.addr.begin(), p->sin6_addr.s6_addr, 16));
    }
}

TEST(SocketTest, AddressCompare)
{
    EXPECT_NE(Address(), Address("unix:///foo/bar"));
    EXPECT_EQ(Address("unix:///foo/bar"), Address("unix:///foo/bar"));
}

TEST(SocketTest, AddressCopy)
{
    Address a;
    Address b("unix:///foo/bar");
    a = b;
    EXPECT_EQ(a, b);
    a = *b.addr();
    EXPECT_EQ(a, b);
}

TEST(SocketTest, AddressInfo)
{
    struct test {
        string url;
        int port;
        vector<uint8_t> addr;
    };
    vector<test> tests = {
        {"http://127.0.0.1:1234", 1234, {127,0,0,1}},
        {"nfs://127.0.0.1",       2049, {127,0,0,1}},
        {"http://localhost",      80, {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1}},
    };
    for (auto t: tests) {
        auto addrs = getAddressInfo(t.url, "tcp");
        EXPECT_GE(addrs.size(), 0);
        auto ai = addrs[0];
        EXPECT_EQ(true, ai.family == AF_INET || ai.family == AF_INET6);
        EXPECT_EQ(SOCK_STREAM, ai.socktype);
        if (ai.family == AF_INET) {
            auto p = reinterpret_cast<const sockaddr_in*>(ai.addr.addr());
            EXPECT_EQ(sizeof(sockaddr_in), p->sin_len);
            EXPECT_EQ(t.port, ntohs(p->sin_port));
            EXPECT_EQ(4, t.addr.size());
            EXPECT_EQ(0, memcmp(t.addr.data(), &p->sin_addr.s_addr, 4));
        }
        else {
            auto p = reinterpret_cast<const sockaddr_in6*>(ai.addr.addr());
            EXPECT_EQ(sizeof(sockaddr_in6), p->sin6_len);
            EXPECT_EQ(t.port, ntohs(p->sin6_port));
            EXPECT_EQ(16, t.addr.size());
            EXPECT_EQ(0, memcmp(t.addr.data(), p->sin6_addr.s6_addr, 16));
        }
    }
}

TEST(SocketTest, Uaddr)
{
    struct test {
        string uaddr;
        int port;
        vector<uint8_t> addr;
    };
    vector<test> tests = {
        {"0.0.0.0.0.111", 111,  {0,0,0,0}},
        {"::.0.111",      111,  {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 }},
        {"1234::1.8.1",   2049, {0x12,0x34,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 }},
    };
    for (auto t: tests) {
        AddressInfo ai = uaddr2taddr(t.uaddr, "tcp");
        EXPECT_EQ(true, ai.family == AF_INET || ai.family == AF_INET6);
        EXPECT_EQ(SOCK_STREAM, ai.socktype);
        if (ai.family == AF_INET) {
            auto p = reinterpret_cast<const sockaddr_in*>(ai.addr.addr());
            EXPECT_EQ(sizeof(sockaddr_in), p->sin_len);
            EXPECT_EQ(t.port, ntohs(p->sin_port));
            EXPECT_EQ(4, t.addr.size());
            EXPECT_EQ(0, memcmp(t.addr.data(), &p->sin_addr.s_addr, 4));
        }
        else {
            auto p = reinterpret_cast<const sockaddr_in6*>(ai.addr.addr());
            EXPECT_EQ(sizeof(sockaddr_in6), p->sin6_len);
            EXPECT_EQ(t.port, ntohs(p->sin6_port));
            EXPECT_EQ(16, t.addr.size());
            EXPECT_EQ(0, memcmp(t.addr.data(), p->sin6_addr.s6_addr, 16));
        }
    }
}
