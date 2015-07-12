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

    int32_t echo(int32_t&& val) override
    {
        return val;
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

}
