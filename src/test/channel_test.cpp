/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <sys/socket.h>
#include <sys/un.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/errors.h>
#include <rpc++/server.h>
#include <gtest/gtest.h>
#include <glog/logging.h>

using namespace oncrpc;
using namespace std;

namespace {

class ChannelTest: public ::testing::Test
{
public:
    ChannelTest()
        : client(make_shared<Client>(1234, 1))
    {
    }

    Address makeLocalAddress(int id)
    {
        ostringstream ss;
        ss << "unix:///tmp/rpcTest-" << ::getpid() << "-" << id;
        return Address::fromUrl(ss.str());
    }

    void unlinkLocalAddress(const Address& addr)
    {
        auto p = reinterpret_cast<const sockaddr_un*>(addr.addr());
        ::unlink(p->sun_path);
    }

    /// Call a simple echo service with the given procedure number
    void simpleCall(
        shared_ptr<Channel> chan, shared_ptr<Client> client, uint32_t proc)
    {
        chan->call(
            client.get(), proc,
            [](XdrSink* xdrs) {
                uint32_t v = 123; xdr(v, xdrs); },
            [](XdrSource* xdrs) {
                uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); });
    }

    future<void> simpleCallAsync(
        shared_ptr<Channel> chan, shared_ptr<Client> client, uint32_t proc,
        Channel::clock_type::duration timeout = 30s)
    {
        return chan->callAsync(
            client.get(), proc,
            [](XdrSink* xdrs) {
                uint32_t v = 123; xdr(v, xdrs); },
            [](XdrSource* xdrs) {
                uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); },
            Protection::DEFAULT, timeout);
    }

    thread callMany(
        shared_ptr<Channel> chan, uint32_t proc, int iterations)
    {
        return thread(
            [=]() {
                for (int i = 0; i < iterations; i++) {
                    simpleCall(chan, client, 1);
                }
            });
    }

    shared_ptr<Client> client;
};

class SimpleServer
{
public:
    /// Procedure 1 is a simple echo service, and procedure
    /// 2 is a stop request. Note, we ignore most of the
    /// RPC protocol for simplicity
    SimpleServer()
    {
    }

    void start()
    {
        thread_ = thread(
            [=]() {
                auto buf = std::make_unique<XdrMemory>(1500);

                bool stopping = false;
                while (!stopping) {
                    auto dec = acquireBuffer();
                    rpc_msg call_msg;
                    uint32_t val;

                    xdr(call_msg, dec);
                    auto& cbody = call_msg.cbody();
                    VLOG(3) << "xid: " << call_msg.xid << ": received call";
                    if (cbody.proc == 1)
                        xdr(val, dec);
                    sendMessage();

                    accepted_reply ar;
                    ar.verf = { AUTH_NONE, {} };
                    ar.stat = SUCCESS;
                    rpc_msg reply_msg(call_msg.xid, reply_body(std::move(ar)));

                    auto enc = receiveMessage();
                    xdr(reply_msg, enc);
                    if (cbody.proc == 1)
                        xdr(val, enc);
                    endReceive();
                    VLOG(3) << "xid: " << call_msg.xid << ": sent reply";

                    if (cbody.proc == 2)
                        stopping = true;
                }
            });
    }

    ~SimpleServer()
    {
        thread_.join();
    }

    void stop(shared_ptr<Channel> chan, shared_ptr<Client> client)
    {
        chan->call(
            client.get(), 2, [](XdrSink* xdrs) {}, [](XdrSource* xdrs) {});
    }

    virtual XdrSource* acquireBuffer() = 0;
    virtual void sendMessage() = 0;
    virtual XdrSink* receiveMessage() = 0;
    virtual void endReceive() = 0;

    thread thread_;
};

class SimpleDatagramServer: public SimpleServer
{
public:
    SimpleDatagramServer(int sock)
        : sock_(sock),
          buf_(make_unique<XdrMemory>(1500))
    {
        start();
    }

    ~SimpleDatagramServer()
    {
        ::close(sock_);
    }

    XdrSource* acquireBuffer() override
    {
        buf_->rewind();
        addrlen_ = addr_.sun_len = sizeof(addr_);
        auto bytes = ::recvfrom(
            sock_, buf_->buf(), buf_->bufferSize(), 0,
            reinterpret_cast<sockaddr*>(&addr_), &addrlen_);
        if (bytes < 0)
            throw system_error(errno, system_category());
        return buf_.get();
    }

    void sendMessage() override
    {
    }

    XdrSink* receiveMessage() override
    {
        buf_->rewind();
        return buf_.get();
    }

    void endReceive() override
    {
        auto n = buf_->writePos();
        auto bytes = ::sendto(
            sock_, buf_->buf(), n, 0,
            reinterpret_cast<const sockaddr*>(&addr_), addrlen_);
        ASSERT_EQ(n, bytes);
    }

    int sock_;
    sockaddr_un addr_;
    socklen_t addrlen_;
    unique_ptr<XdrMemory> buf_;
};

class SimpleStreamServer: public SimpleServer
{
public:
    SimpleStreamServer(int sock)
        : sock_(sock)
    {
        dec_ = std::make_unique<RecordReader>(
            1500,
            [=](void* buf, size_t len) {
                return ::read(sock_, buf, len);
            });
        enc_ = std::make_unique<RecordWriter>(
            1500,
            [=](const void* buf, size_t len) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
                size_t n = len;
                while (n > 0) {
                    auto bytes = ::write(sock_, p, len);
                    if (bytes < 0)
                        throw std::system_error(
                            errno, std::system_category());
                    p += bytes;
                    n -= bytes;
                }
                return len;
            });
        start();
    }

    ~SimpleStreamServer()
    {
        ::close(sock_);
    }

    XdrSource* acquireBuffer() override
    {
        return dec_.get();
    }

    void sendMessage() override
    {
        dec_->endRecord();
    }

    XdrSink* receiveMessage() override
    {
        return enc_.get();
    }

    void endReceive() override
    {
        enc_->pushRecord();
    }

    int sock_;
    unique_ptr<RecordReader> dec_;
    unique_ptr<RecordWriter> enc_;
};

/// An RPC channel that just simulates a timeout
class TimeoutChannel: public Channel
{
public:
    unique_ptr<XdrSink> acquireSendBuffer() override
    {
        return make_unique<XdrMemory>(buf_, sizeof(buf_));
    }

    void releaseSendBuffer(unique_ptr<XdrSink>&& msg) override
    {
        msg.reset();
    }

    void sendMessage(unique_ptr<XdrSink>&& msg) override
    {
        msg.reset();
    }

    unique_ptr<XdrSource> receiveMessage(
        shared_ptr<Channel>&, std::chrono::system_clock::duration timeout) override
    {
        return nullptr;
    }

    void releaseReceiveBuffer(unique_ptr<XdrSource>&& msg) override
    {
        msg.reset();
    }

    uint8_t buf_[1500];
};

TEST_F(ChannelTest, Message)
{
    // Three parts - one word, a reference to four bytes and a second word
    struct test {
        int foo;
        shared_ptr<Buffer> bar;
        int baz;

        int operator==(const test& other) const
        {
            if (foo != other.foo || baz != other.baz)
                return false;
            if (bar->size() != other.bar->size())
                return false;
            for (size_t i = 0; i < bar->size(); i++)
                if (bar->data()[i] != other.bar->data()[i])
                    return false;
            return true;
        }
    };

    uint8_t buf[4] = {1, 2, 3, 4};
    test t1 {1234, make_shared<Buffer>(4, buf), 5678};

    Message msg(12);
    xdr(t1.foo, static_cast<XdrSink*>(&msg));
    xdr(t1.bar, static_cast<XdrSink*>(&msg));
    xdr(t1.baz, static_cast<XdrSink*>(&msg));
    msg.flush();

    test t2;
    xdr(t2.foo, static_cast<XdrSource*>(&msg));
    xdr(t2.bar, static_cast<XdrSource*>(&msg));
    xdr(t2.baz, static_cast<XdrSource*>(&msg));

    EXPECT_EQ(t1, t2);
}

TEST_F(ChannelTest, Basic)
{
    TimeoutChannel channel;

    // XXX need an exception type for timeout
    EXPECT_THROW(
        channel.call(client.get(), 1,
                     [](XdrSink* xdrs) {},
                     [](XdrSource* xdrs) {},
                     Protection::DEFAULT,
                     chrono::milliseconds(1)),
        TimeoutError);
}

TEST_F(ChannelTest, LocalChannel)
{
    auto svcreg = make_shared<ServiceRegistry>();
    auto channel = make_shared<LocalChannel>(svcreg);

    // We have no services so any call should return PROG_UNAVAIL
    EXPECT_THROW(simpleCall(channel, client, 1), ProgramUnavailable);

    // Add a service handler for program 1234, version 2
    auto handler = [](CallContext&& ctx)
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
        };
    svcreg->add(1234, 2, handler);

    // Try calling with the wrong version number and check for
    // PROG_MISMATCH
    EXPECT_THROW(simpleCall(channel, client, 1), VersionMismatch);

    // Change the client to specify verision 2
    client = make_shared<Client>(1234, 2);

    // Check PROC_UNAVAIL
    EXPECT_THROW(simpleCall(channel, client, 2), ProcedureUnavailable);

    // Call again with the right version number and procedure
    simpleCall(channel, client, 1);

    // Unregister our handler and verify that we get PROG_UNAVAIL again
    svcreg->remove(1234, 2);
    EXPECT_THROW(simpleCall(channel, client, 1), ProgramUnavailable);
}

TEST_F(ChannelTest, LocalManyThreads)
{
    auto svcreg = make_shared<ServiceRegistry>();
    auto chan = std::make_shared<LocalChannel>(svcreg);

    // Add a service handler for program 1234, version 1
    auto handler = [](CallContext&& ctx)
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
        };
    svcreg->add(1234, 1, handler);

    int threadCount = 20;
    int iterations = 200;

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
        threads.push_back(callMany(chan, 1, iterations));

    for (auto& t: threads)
        t.join();
}

TEST_F(ChannelTest, DatagramChannel)
{
    // Make a local socket to listen on
    Address saddr = makeLocalAddress(0);
    int ssock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    ASSERT_GE(::bind(ssock, saddr.addr(), saddr.len()), 0);

    SimpleDatagramServer server(ssock);

    // Make a suitable address for the channel to receive replies
    Address caddr = makeLocalAddress(1);

    // Bind to our reply address and connect to the server address
    int clsock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    auto chan = make_shared<DatagramChannel>(clsock);
    chan->bind(caddr);
    chan->connect(saddr);

    // Send a message and check the reply
    simpleCall(chan, client, 1);

    server.stop(chan, client);

    unlinkLocalAddress(saddr);
    unlinkLocalAddress(caddr);
}

TEST_F(ChannelTest, DatagramManyThreads)
{
    // Make a local socket to listen on
    Address saddr = makeLocalAddress(0);
    int ssock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    ASSERT_GE(::bind(ssock, saddr.addr(), saddr.len()), 0);

    SimpleDatagramServer server(ssock);

    // Make a suitable address for the channel to receive replies
    Address caddr = makeLocalAddress(1);

    // Bind to our reply address and connect to the server address
    int clsock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    auto chan = make_shared<DatagramChannel>(clsock);
    chan->bind(caddr);
    chan->connect(saddr);

    int threadCount = 20;
    int iterations = 200;

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
        threads.push_back(callMany(chan, 1, iterations));

    for (auto& t: threads)
        t.join();

    // Ask the server to stop running
    server.stop(chan, client);

    unlinkLocalAddress(saddr);
    unlinkLocalAddress(caddr);
}

TEST_F(ChannelTest, StreamChannel)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    int ssock = sockpair[0];
    int clsock = sockpair[1];

    SimpleStreamServer server(ssock);

    // Send a message and check the reply
    auto chan = make_shared<StreamChannel>(clsock);
    simpleCall(chan, client, 1);

    // Ask the server to stop running
    server.stop(chan, client);
}

TEST_F(ChannelTest, StreamManyThreads)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    int ssock = sockpair[0];
    int clsock = sockpair[1];

    SimpleStreamServer server(ssock);

    auto chan = make_shared<StreamChannel>(clsock);

    int threadCount = 20;
    int iterations = 200;

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
        threads.push_back(callMany(chan, 1, iterations));

    for (auto& t: threads)
        t.join();

    // Ask the server to stop running
    server.stop(chan, client);
}

TEST_F(ChannelTest, BadReply)
{
    auto svcreg = make_shared<ServiceRegistry>();
    auto channel = make_shared<LocalChannel>(svcreg);

    auto handler = [](CallContext&& ctx)
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
        };
    svcreg->add(1234, 1, handler);

    EXPECT_THROW(
        channel->call(
            client.get(), 1,
            [](XdrSink* xdrs) {
                uint32_t v = 123; xdr(v, xdrs); },
            [](XdrSource* xdrs) {
                uint64_t v; xdr(v, xdrs); }),
        XdrError);
}

TEST_F(ChannelTest, LocalCallAsync)
{
    auto svcreg = make_shared<ServiceRegistry>();
    auto chan = std::make_shared<LocalChannel>(svcreg);

    // Add a service handler for program 1234, version 1
    auto handler = [](CallContext&& ctx)
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
        };
    svcreg->add(1234, 1, handler);

    auto f = simpleCallAsync(chan, client, 1);
    chan->processReply();
    f.get();
}

TEST_F(ChannelTest, StreamCallAsync)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    int ssock = sockpair[0];
    int clsock = sockpair[1];

    SimpleStreamServer server(ssock);
    auto chan = make_shared<StreamChannel>(clsock);

    // Read replies asynchronously using a SockerManager.
    SocketManager sockman;
    sockman.add(chan);
    thread t([&]() { sockman.run(); });

    auto f = simpleCallAsync(chan, client, 1);
    f.get();

    server.stop(chan, client);
    sockman.stop();
    t.join();
}

TEST_F(ChannelTest, LocalAsyncTimeout)
{
    auto svcreg = make_shared<ServiceRegistry>();
    TimeoutManager tman;
    auto chan = std::make_shared<LocalChannel>(svcreg);
    chan->setTimeoutManager(&tman);
    auto f = simpleCallAsync(chan, client, 1, 5ms);
    std::this_thread::sleep_for(10ms);
    chan->processReply();
    EXPECT_THROW(f.get(), TimeoutError);
}

TEST_F(ChannelTest, StreamAsyncTimeout)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    int clsock = sockpair[1];

    auto chan = make_shared<StreamChannel>(clsock);

    // Read replies asynchronously using a SockerManager.
    SocketManager sockman;
    sockman.add(chan);
    chan->setTimeoutManager(&sockman);
    thread t([&]() { sockman.run(); });

    auto f = simpleCallAsync(chan, client, 1, 5ms);
    std::this_thread::sleep_for(10ms);
    EXPECT_THROW(f.get(), TimeoutError);

    sockman.stop();
    t.join();
}

}
