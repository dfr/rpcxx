/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <thread>
#include <sys/socket.h>
#include <sys/un.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/rpcproto.h>
#include <rpc++/server.h>
#include <rpc++/sockman.h>
#include <rpc++/xdr.h>
#include <gtest/gtest.h>
#include <glog/logging.h>

using namespace oncrpc;
using namespace std;
using namespace std::placeholders;

namespace {

class ServerTest: public ::testing::Test
{
public:
    ServerTest()
        : svcreg(make_shared<ServiceRegistry>()),
          client(make_shared<Client>(1234, 1))
    {
        addTestService();
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
        svcreg->add(1234, 1, bind(&ServerTest::testService, this, _1));
    }

    rpc_msg sendMessage(
        rpc_msg&& call, const vector<uint8_t>& args, const vector<uint8_t>& res)
    {
        auto chan = make_shared<LocalChannel>(svcreg);

        auto xdrout = chan->acquireSendBuffer();
        xdr(call, xdrout.get());
        xdrout->putBytes(args.data(), args.size());
        chan->sendMessage(move(xdrout));

        shared_ptr<Channel> p;
        auto xdrin = chan->receiveMessage(p, 0s);
        rpc_msg reply;
        xdr(reply, static_cast<XdrSource*>(xdrin.get()));
        if (reply.rbody().stat == MSG_ACCEPTED) {
            vector<uint8_t> t;
            t.resize(res.size());
            xdrin->getBytes(t.data(), t.size());
            EXPECT_EQ(res, t);
        }
        chan->releaseReceiveBuffer(move(xdrin));

        return reply;
    }

    void checkReply(uint32_t prog, uint32_t vers, uint32_t proc,
                    accept_stat expectedStat,
                    const vector<uint8_t>& args,
                    const vector<uint8_t>& res,
                    rpc_msg* reply_msg = nullptr)
    {
        auto chan = make_shared<LocalChannel>(svcreg);

        call_body cbody;
        cbody.prog = prog;
        cbody.vers = vers;
        cbody.proc = proc;
        cbody.cred = { AUTH_NONE, {} };
        cbody.verf = { AUTH_NONE, {} };
        rpc_msg msg(1, std::move(cbody));
        msg = sendMessage(move(msg), args, res);

        EXPECT_EQ(1, msg.xid);
        EXPECT_EQ(REPLY, msg.mtype);
        EXPECT_EQ(MSG_ACCEPTED, msg.rbody().stat);
        EXPECT_EQ(expectedStat, msg.rbody().areply().stat);

        if (reply_msg)
            *reply_msg = std::move(msg);
    }

    shared_ptr<ServiceRegistry> svcreg;
    shared_ptr<Client> client;
};

/// A datagram socket using AF_LOCAL sockets, autogenerating a random
/// local address
class LocalDatagramChannel: public DatagramChannel
{
public:
    LocalDatagramChannel(shared_ptr<ServiceRegistry> svcreg = nullptr)
        : DatagramChannel(::socket(AF_LOCAL, SOCK_DGRAM, 0), svcreg),
          path_(makePath()),
          addr_(Address::fromUrl("unix://" + path_))
    {
        bind(localAddr());
    }

    ~LocalDatagramChannel()
    {
        ::unlink(path_.c_str());
    }

    const Address& localAddr() const
    {
        return addr_;
    }

    static string makePath()
    {
        static int index = 0;
        ostringstream ss;
        ss << "/tmp/rpcTest-" << ::getpid() << "-" << index++;
        return ss.str();
    }

private:
    string path_;
    Address addr_;
};

TEST_F(ServerTest, Lookup)
{
    EXPECT_NE(svcreg->lookup(1234, 1), nullptr);
}

TEST_F(ServerTest, ProtocolMismatch)
{
    auto chan = make_shared<LocalChannel>(svcreg);

    // Check RPC_MISMATCH is generated for rpcvers other than 2
    rpc_msg call(1, call_body(1234, 0, 0, {AUTH_DH, {}}, {AUTH_NONE, {}}));
    call.cbody().rpcvers = 3;
    auto msg = sendMessage(move(call), {}, {});
    EXPECT_EQ(1, msg.xid);
    EXPECT_EQ(REPLY, msg.mtype);
    EXPECT_EQ(MSG_DENIED, msg.rbody().stat);
    EXPECT_EQ(RPC_MISMATCH, msg.rbody().rreply().stat);
    EXPECT_EQ(2, msg.rbody().rreply().rpc_mismatch.low);
    EXPECT_EQ(2, msg.rbody().rreply().rpc_mismatch.high);
}

TEST_F(ServerTest, ProgramUnavailable)
{
    checkReply(1235, 1, 0, PROG_UNAVAIL, {}, {});
}

TEST_F(ServerTest, ProgramMismatch)
{
    rpc_msg reply_msg;
    checkReply(1234, 2, 0, PROG_MISMATCH, {}, {}, &reply_msg);
    EXPECT_EQ(1, reply_msg.rbody().areply().mismatch_info.low);
    EXPECT_EQ(1, reply_msg.rbody().areply().mismatch_info.high);
}

TEST_F(ServerTest, ProcedureUnavailable)
{
    checkReply(1234, 1, 2, PROC_UNAVAIL, {}, {});
}

TEST_F(ServerTest, GarbageArgs)
{
    checkReply(1234, 1, 1, GARBAGE_ARGS, {}, {});
}

TEST_F(ServerTest, Success)
{
    checkReply(1234, 1, 0, SUCCESS, {}, {});
    checkReply(1234, 1, 1, SUCCESS, {1,2,3,4}, {1,2,3,4});
}

TEST_F(ServerTest, AuthUnsupported)
{
    auto msg = sendMessage(
        rpc_msg(1, call_body(1234, 0, 0, {AUTH_DH, {}}, {AUTH_NONE, {}})),
        {}, {});

    EXPECT_EQ(1, msg.xid);
    EXPECT_EQ(REPLY, msg.mtype);
    EXPECT_EQ(MSG_DENIED, msg.rbody().stat);
    EXPECT_EQ(AUTH_ERROR, msg.rbody().rreply().stat);
    EXPECT_EQ(AUTH_BADCRED, msg.rbody().rreply().auth_error);
}

TEST_F(ServerTest, AuthBadGssCred)
{
    auto msg = sendMessage(
        rpc_msg(1, call_body(1234, 0, 0,
            {RPCSEC_GSS, {1, 2, 3}}, {AUTH_NONE, {}})),
        {}, {});

    EXPECT_EQ(1, msg.xid);
    EXPECT_EQ(REPLY, msg.mtype);
    EXPECT_EQ(MSG_DENIED, msg.rbody().stat);
    EXPECT_EQ(AUTH_ERROR, msg.rbody().rreply().stat);
    EXPECT_EQ(AUTH_BADCRED, msg.rbody().rreply().auth_error);
}

TEST_F(ServerTest, Datagram)
{
    auto schan = make_shared<LocalDatagramChannel>(svcreg);
    auto cchan = make_shared<LocalDatagramChannel>();
    schan->connect(cchan->localAddr());
    cchan->connect(schan->localAddr());

    auto sockman = make_shared<SocketManager>();
    sockman->add(schan);
    thread server([sockman]() { sockman->run(); });

    // Send a message and check the reply
    cchan->call(
        client.get(), 1,
        [](XdrSink* xdrs) { uint32_t v = 123; xdr(v, xdrs); },
        [](XdrSource* xdrs) { uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); });

    sockman->stop();
    server.join();
}

TEST_F(ServerTest, Stream)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    auto chan = make_shared<StreamChannel>(sockpair[0]);

    auto sockman = make_shared<SocketManager>();
    sockman->add(make_shared<StreamChannel>(sockpair[1], svcreg));
    thread server([sockman]() { sockman->run(); });

    // Send a message and check the reply
    chan->call(
        client.get(), 1,
        [](XdrSink* xdrs) { uint32_t v = 123; xdr(v, xdrs); },
        [](XdrSource* xdrs) { uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); });

    sockman->stop();
    server.join();
}

TEST_F(ServerTest, Listen)
{
    // Make a local socket to listen on
    ostringstream ss;
    ss << "/tmp/rpcTest-" << ::getpid();
    auto sockname = ss.str();
    sockaddr_un sun;
    sun.sun_len = sizeof(sun);
    sun.sun_family = AF_LOCAL;
    strcpy(sun.sun_path, sockname.c_str());
    int lsock = socket(AF_LOCAL, SOCK_STREAM, 0);
    ASSERT_GE(::bind(lsock, reinterpret_cast<sockaddr*>(&sun), sizeof(sun)), 0);
    ASSERT_GE(::listen(lsock, 5), 0);

    auto sockman = make_shared<SocketManager>();
    sockman->add(make_shared<ListenSocket>(lsock, svcreg));
    thread server([sockman]() { sockman->run(); });

    // Test connecting to the socket
    int sock = socket(AF_LOCAL, SOCK_STREAM, 0);
    ASSERT_GE(::connect(
                  sock, reinterpret_cast<sockaddr*>(&sun), sizeof(sun)), 0);

    // Send a message and check the reply
    auto chan = make_shared<StreamChannel>(sock);
    chan->call(
        client.get(), 1,
        [](XdrSink* xdrs) { uint32_t v = 123; xdr(v, xdrs); },
        [](XdrSource* xdrs) { uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); });

    sockman->stop();
    server.join();

    EXPECT_GE(::unlink(sun.sun_path), 0);
}

struct ThreadPool
{
    ThreadPool(Service svc, int workerCount)
        : pending_(0)
    {
        for (int i = 0; i < workerCount; i++)
            workers_.emplace_back(this, svc);
    }

    ~ThreadPool()
    {
        EXPECT_EQ(pending_, 0);
    }

    void dispatch(CallContext&& ctx)
    {
        pending_ += ctx.size();
        VLOG(2) << "xid: " << ctx.msg().xid
                << ": dispatching to worker " << nextWorker_
                << ", pending: " << pending_ << " bytes";
        workers_[nextWorker_].add(move(ctx));
        nextWorker_++;
        if (nextWorker_ == int(workers_.size()))
            nextWorker_ = 0;
    }

    struct Worker
    {
        Worker(ThreadPool* pool, Service svc)
            : pool_(pool),
              svc_(svc)
        {
            thread_ = thread([this]() {
                unique_lock<mutex> lock(mutex_);
                while (!stopping_) {
                    VLOG(2) << "queue size " << work_.size();
                    if (work_.size() > 0) {
                        auto ctx = move(work_.front());
                        work_.pop_front();
                        lock.unlock();
                        //std::this_thread::sleep_for(10ms);
                        VLOG(2) << "xid: " << ctx.msg().xid << ": running";
                        ctx();
                        pool_->pending_ -= ctx.size();
                        lock.lock();
                    }
                    if (work_.size() == 0)
                        cv_.wait(lock);
                }
            });
        }

        ~Worker()
        {
            stopping_ = true;
            cv_.notify_one();
            thread_.join();
        }

        void add(CallContext&& ctx)
        {
            ctx.setService(svc_);
            unique_lock<mutex> lock(mutex_);
            work_.emplace_back(move(ctx));
            cv_.notify_one();
        }

        thread thread_;
        mutex mutex_;
        condition_variable cv_;
        deque<CallContext> work_;
        bool stopping_ = false;
        ThreadPool* pool_;
        Service svc_;
    };

    deque<Worker> workers_;
    int nextWorker_ = 0;
    atomic<int> pending_;
};

TEST_F(ServerTest, MultiThread)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    auto cchan = make_shared<StreamChannel>(sockpair[0]);
    auto schan = make_shared<StreamChannel>(sockpair[1], svcreg);

    auto sockman = make_shared<SocketManager>();
    sockman->add(cchan);
    sockman->add(schan);
    thread t([sockman]() { sockman->run(); });


    // Wrap the test service with a simple thread pool dispatcher
    auto svc = svcreg->lookup(1234, 1);
    svcreg->remove(1234, 1);
    ThreadPool pool(svc, 10);
    svcreg->add(1234, 1, bind(&ThreadPool::dispatch, &pool, _1));

    int callCount = 1000;
    int maxPending = 100;
    deque<future<void>> calls;
    for (int i = 0; i < callCount; i++) {
        auto f = cchan->callAsync(
            client.get(), 1,
            [](XdrSink* xdrs) {
                uint32_t v = 123; xdr(v, xdrs); },
            [](XdrSource* xdrs) {
                uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); });
        calls.emplace_back(move(f));
        while (int(calls.size()) > maxPending) {
            calls.front().get();
            calls.pop_front();
        }
    }
    for (auto& f: calls)
        f.get();

    sockman->stop();
    t.join();
}

}
