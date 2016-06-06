/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <sys/wait.h>
#include <signal.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/cred.h>
#include <rpc++/server.h>
#include <rpc++/socket.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <glog/logging.h>

DEFINE_string(keytab, "./data/krb5/krb5.keytab", "Kerberos keytab");
DEFINE_string(krb5config, "./data/krb5/krb5.conf", "Kerberos config");
DEFINE_string(runkdc, "./data/krb5/run-kdc.sh", "Script to start test KDC");

using namespace oncrpc;
using namespace oncrpc::_detail;
using namespace std;
using namespace std::placeholders;
using namespace std::literals::chrono_literals;

namespace {

class GssEnv: public ::testing::Environment
{
public:
    GssEnv()
    {
        AddGlobalTestEnvironment(this);
    }

    void SetUp() override
    {
        LOG(INFO) << "GssEnv::SetUp";
        pid_ = ::fork();
        if (pid_ == 0) {
            LOG(INFO) << "child!";
            ::setpgid(0, 0);
            system(FLAGS_runkdc.c_str());
            ::_Exit(0);
        }
        auto addrs = getAddressInfo("tcp://localhost:8888", "tcp");
        auto& ai = addrs[0];
        for (;;) {
            int fd = socket(ai.family, ai.socktype, ai.protocol);
            if (fd > 0) {
                Socket sock(fd);
                try {
                    sock.connect(ai.addr);
                } catch (system_error& e) {
                    if (e.code().value() == ECONNREFUSED) {
                        this_thread::sleep_for(10ms);
                        continue;
                    }
                    throw;
                }
                LOG(INFO) << "KDC is live";
                sock.close();
                break;
            }
        }
    }

    void TearDown() override
    {
        LOG(INFO) << "GssEnv::TearDown";
        ::killpg(pid_, SIGTERM);
        ::waitpid(-pid_, nullptr, 0);
    }

private:
    pid_t pid_;
};

GssEnv* gssenv = new GssEnv;

class FakeCredMapper: public CredMapper
{
public:
    bool lookupCred(const std::string& name, Credential& cred) override
    {
	cred = Credential(1234, 5678, {}, false);
	return true;
    }
};

class GssTest: public ::testing::Test
{
public:
    GssTest()
        : svcreg(make_shared<ServiceRegistry>())
    {
	svcreg->mapCredentials("TEST_REALM", make_shared<FakeCredMapper>());
        addTestService();
        ::setenv("KRB5_KTNAME", FLAGS_keytab.c_str(), true);
        ::setenv("KRB5_CONFIG", FLAGS_krb5config.c_str(), true);
        string initiator = "test";
        string principal = "test@localhost";
        client = make_shared<GssClient>(
            1234, 1, initiator, principal, "krb5", GssService::NONE);
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
