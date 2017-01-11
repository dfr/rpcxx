/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <sstream>

#include <gtest/gtest.h>

#include <rpc++/client.h>
#include <rpc++/errors.h>
#include <rpc++/gss.h>
#include <rpc++/server.h>

#include "utils/rpcgen/test/test.h"

using namespace oncrpc;
using namespace std;
using namespace std::placeholders;

namespace {

class ServerTest: public ::testing::Test
{
public:
    ServerTest()
    {
        svcreg = make_shared<ServiceRegistry>();
    }

    shared_ptr<ServiceRegistry> svcreg;
};

class Test1Impl: public Test1Service
{
public:
    void null() override
    {
    }

    int32_t echo(const int32_t& val) override
    {
        return val;
    }

    foolist list() override
    {
        return nullptr;
    }

    bar getbar() override
    {
        return bar(1, 2);
    }

    int32_t write(const writereq& req) override
    {
        int sum = 0;
        auto p = req.buf->data();
        auto sz = int(req.buf->size());
        for (auto i = 0; i < sz; i++)
            sum += p[i];
        return sum;
    }
};

TEST_F(ServerTest, Call)
{
    auto chan = make_shared<LocalChannel>(svcreg);
    auto srv = make_shared<Test1Impl>();
    srv->bind(svcreg);
    Test1<> client(chan);
    EXPECT_EQ(1234, client.echo(1234));
    srv->unbind(svcreg);
    EXPECT_THROW(client.null(), ProgramUnavailable);
}

TEST_F(ServerTest, CallRef)
{
    auto chan = make_shared<LocalChannel>(svcreg);
    auto srv = make_shared<Test1Impl>();
    srv->bind(svcreg);
    Test1<> client(chan);

    uint8_t buf[] = {1, 2, 3, 4};
    EXPECT_EQ(10, client.write(writereq{make_shared<Buffer>(4, buf)}));
}

}
