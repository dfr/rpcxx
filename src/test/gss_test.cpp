#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/server.h>

#include <gtest/gtest.h>
#include <glog/logging.h>

using namespace oncrpc;
using namespace oncrpc::_detail;
using namespace std;
using namespace std::placeholders;
using namespace std::literals::chrono_literals;

namespace {

class GssTest: public ::testing::Test
{
public:
    GssTest()
        : svcreg(make_shared<ServiceRegistry>())
    {
        addTestService();
        ::setenv("KRB5_KTNAME", "test.keytab", true);
        char hbuf[512];
        ::gethostname(hbuf, sizeof(hbuf));
        string principal = "host@";
        principal += hbuf;
        client = make_shared<GssClient>(
            1234, 1, principal, "krb5", GssService::NONE);
    }

    void testService(CallContext&& ctx)
    {
        switch (ctx.proc()) {
        case 0:
            ctx.sendReply([](XdrSink*){});
            break;

        case 1:
            uint32_t val;
            ctx.getArgs([&](XdrSource* xdrs){ xdr(val, xdrs); });
            ctx.sendReply([&](XdrSink* xdrs){ xdr(val, xdrs); });
            break;

        default:
            ctx.procedureUnavailable();
        }
    }

    void addTestService()
    {
        svcreg->add(1234, 1, bind(&GssTest::testService, this, _1));
    }

    /// Call a simple echo service with the given procedure number
    void simpleCall(
        shared_ptr<Channel> chan, uint32_t proc,
        Protection prot = Protection::NONE)
    {
        chan->call(
            client.get(), proc,
            [](XdrSink* xdrs) {
                uint32_t v = 123; xdr(v, xdrs); },
            [](XdrSource* xdrs) {
                uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); },
            prot);
    }

    thread callMany(
        shared_ptr<Channel> chan, uint32_t proc, int iterations)
    {
        return thread(
            [=]() {
                for (int i = 0; i < iterations; i++) {
                    simpleCall(chan, 1);
                }
            });
    }

    shared_ptr<ServiceRegistry> svcreg;
    shared_ptr<GssClient> client;
};

TEST_F(GssTest, SequenceWindow)
{
    SequenceWindow win(5);
    EXPECT_EQ(false, win.valid(0));
    win.update(1);
    EXPECT_EQ(true, win.valid(1));
    win.reset(1);
    EXPECT_EQ(false, win.valid(1));
    win.update(100);
    EXPECT_EQ(false, win.valid(95));
    EXPECT_EQ(true, win.valid(96));
    EXPECT_EQ(true, win.valid(97));
    EXPECT_EQ(true, win.valid(98));
    EXPECT_EQ(true, win.valid(99));
    EXPECT_EQ(true, win.valid(100));
    win.reset(97);
    EXPECT_EQ(false, win.valid(97));
    win.update(101);
    EXPECT_EQ(false, win.valid(96));
    EXPECT_EQ(false, win.valid(97));
}

TEST_F(GssTest, Init)
{
    auto chan = make_shared<LocalChannel>(svcreg);
    for (auto prot = int(Protection::NONE);
         prot <= int(Protection::PRIVACY); ++prot) {
        // Send a message and check the reply
        simpleCall(chan, 1, Protection(prot));
    }
}

TEST_F(GssTest, ReInit)
{
    auto chan = make_shared<LocalChannel>(svcreg);

    // The first call will create a new context and make the call
    simpleCall(chan, 1);

    // Clear the server-side state to make sure the client re-initialises
    svcreg->clearClients();
    simpleCall(chan, 1);
}

TEST_F(GssTest, LocalManyThreads)
{
    auto chan = make_shared<LocalChannel>(svcreg);

    int threadCount = 20;
    int iterations = 200;

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
        threads.push_back(callMany(chan, 1, iterations));

    for (auto& t: threads)
        t.join();
}

TEST_F(GssTest, StreamManyThreads)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);
    auto schan = make_shared<StreamChannel>(sockpair[0], svcreg);
    auto cchan = make_shared<StreamChannel>(sockpair[1]);

    SocketManager sockman;
    sockman.add(schan);
    thread t([&]() { sockman.run(); });

    int threadCount = 20;
    int iterations = 200;

    // Force frequent client expiry
    svcreg->setClientLifetime(100ms);

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
        threads.push_back(callMany(cchan, 1, iterations));

    for (auto& t: threads)
        t.join();

    cchan->close();
    t.join();
}

}
