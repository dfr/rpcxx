/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <sstream>

#include <gtest/gtest.h>

#include <rpc++/client.h>
#include <rpc++/gss.h>
#include <rpc++/server.h>

#include "utils/rpcgen/test/test.h"

using namespace oncrpc;
using namespace std;
using namespace std::placeholders;

namespace {

class ClientTest: public ::testing::Test
{
public:
    ClientTest()
    {
        svcreg = make_shared<ServiceRegistry>();
        addTestService();
    }

    void testService(CallContext&& ctx)
    {
        switch (ctx.proc()) {
        case 0:
            ctx.sendReply([](XdrSink*){});
            break;

        case 1:
            int32_t val;
            ctx.getArgs([&](XdrSource* xdrs){ xdr(val, xdrs); });
            ctx.sendReply([&](XdrSink* xdrs){ xdr(val, xdrs); });
            break;

        default:
            ctx.procedureUnavailable();
        }
    }

    void addTestService()
    {
        svcreg->add(1234, 1, bind(&ClientTest::testService, this, _1));
    }

    shared_ptr<ServiceRegistry> svcreg;
};

TEST_F(ClientTest, Create)
{
    auto chan = make_shared<LocalChannel>(svcreg);
    Test1<> client(chan);
    Test1<GssClient> gssclient(
        chan, "foo@bar", "krb5", GssService::NONE);
}

TEST_F(ClientTest, Call)
{
    auto chan = make_shared<LocalChannel>(svcreg);
    Test1<> client(chan);
    EXPECT_EQ(1234, client.echo(1234));
}

}
