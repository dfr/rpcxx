#include <thread>
#include <sys/socket.h>
#include <sys/un.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/rpcproto.h>
#include <rpc++/server.h>
#include <rpc++/xdr.h>
#include <gtest/gtest.h>

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

    void checkReply(uint32_t prog, uint32_t vers, uint32_t proc,
                    accept_stat expectedStat,
                    const vector<uint8_t>& args,
                    const vector<uint8_t>& res,
                    rpc_msg* reply_msg = nullptr)
    {
        LocalChannel chan(svcreg);

        call_body cbody;
        cbody.prog = prog;
        cbody.vers = vers;
        cbody.proc = proc;
        cbody.cred = { AUTH_NONE, {} };
        cbody.verf = { AUTH_NONE, {} };
        rpc_msg msg(1, std::move(cbody));
        auto xdrout = chan.acquireBuffer();
        xdr(msg, static_cast<XdrSink*>(xdrout.get()));
        xdrout->putBytes(args.data(), args.size());
        chan.sendMessage(move(xdrout));

        auto xdrin = chan.receiveMessage(0s);
        xdr(msg, static_cast<XdrSource*>(xdrin.get()));
        EXPECT_EQ(msg.xid, 1);
        EXPECT_EQ(msg.mtype, REPLY);
        EXPECT_EQ(msg.rbody().stat, MSG_ACCEPTED);
        EXPECT_EQ(msg.rbody().areply().stat, expectedStat);

        vector<uint8_t> t;
        t.resize(res.size());
        xdrin->getBytes(t.data(), t.size());
        EXPECT_EQ(res, t);

        chan.releaseBuffer(move(xdrin));

        if (reply_msg)
            *reply_msg = std::move(msg);
    }

    shared_ptr<ServiceRegistry> svcreg;
    shared_ptr<Client> client;
};

TEST_F(ServerTest, Lookup)
{
    EXPECT_NE(svcreg->lookup(1234, 1), nullptr);
}

TEST_F(ServerTest, ProtocolMismatch)
{
    LocalChannel chan(svcreg);

    // Check RPC_MISMATCH is generated for rpcvers other than 2
    call_body cbody;
    cbody.rpcvers = 3;
    cbody.prog = 1234;
    cbody.vers = 0;
    cbody.proc = 0;
    rpc_msg msg(1, std::move(cbody));
    auto xdrout = chan.acquireBuffer();
    xdr(msg, static_cast<XdrSink*>(xdrout.get()));
    chan.sendMessage(move(xdrout));

    auto xdrin = chan.receiveMessage(0s);
    xdr(msg, static_cast<XdrSource*>(xdrin.get()));
    chan.releaseBuffer(move(xdrin));
    EXPECT_EQ(msg.xid, 1);
    EXPECT_EQ(msg.mtype, REPLY);
    EXPECT_EQ(msg.rbody().stat, MSG_DENIED);
    EXPECT_EQ(msg.rbody().rreply().stat, RPC_MISMATCH);
    EXPECT_EQ(msg.rbody().rreply().rpc_mismatch.low, 2);
    EXPECT_EQ(msg.rbody().rreply().rpc_mismatch.high, 2);
}

TEST_F(ServerTest, ProgramUnavailable)
{
    checkReply(1235, 1, 0, PROG_UNAVAIL, {}, {});
}

TEST_F(ServerTest, ProgramMismatch)
{
    rpc_msg reply_msg;
    checkReply(1234, 2, 0, PROG_MISMATCH, {}, {}, &reply_msg);
    EXPECT_EQ(reply_msg.rbody().areply().mismatch_info.low, 1);
    EXPECT_EQ(reply_msg.rbody().areply().mismatch_info.high, 1);
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

    // Close this side of the socket pair which should prompt the
    // connection registry to stop when it notices the end-of-file
    chan->close();
    chan.reset();
    server.join();
}

TEST_F(ServerTest, Listen)
{
    // Make a local socket to listen on
    char tmp[] = "/tmp/rpcTestXXXXX";
    auto sockname = mktemp(tmp);
    sockaddr_un sun;
    sun.sun_len = sizeof(sun);
    sun.sun_family = AF_LOCAL;
    strcpy(sun.sun_path, sockname);
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

    // Close this side of the socket pair. The connection registry
    // won't stop running automatically since the listen socket is
    // still valid so we tell it to stop.
    sockman->stop();
    chan->close();
    chan.reset();
    server.join();

    EXPECT_GE(::unlink(sun.sun_path), 0);
}

struct ThreadPool
{
    ThreadPool(Service svc, int workerCount)
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
        if (nextWorker_ == workers_.size())
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
        while (calls.size() > maxPending) {
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
