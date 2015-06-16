#include <sys/socket.h>
#include <sys/un.h>

#include <rpc++/channel.h>
#include <rpc++/server.h>
#include <gtest/gtest.h>


using namespace oncrpc;
using namespace std;

namespace {

class ChannelTest: public ::testing::Test
{
public:
    /// Call a simple echo service with the given procedure number
    void simpleCall(shared_ptr<Channel> chan, uint32_t vers, uint32_t proc)
    {
	chan->call(
	    1234, vers, proc,
	    [](XdrSink* xdrs) {
		uint32_t v = 123; xdr(v, xdrs); },
	    [](XdrSource* xdrs) {
		uint32_t v; xdr(v, xdrs); EXPECT_EQ(v, 123); });
    }

    thread callMany(shared_ptr<Channel> chan, uint32_t proc, int iterations)
    {
	return thread(
	    [=]() {
		for (int i = 0; i < iterations; i++) {
		    simpleCall(chan, 1, 1);
		}
	    });
    }
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
		    auto dec = beginCall();
		    rpc_msg call_msg;
		    uint32_t val;
		    xdr(call_msg, dec);
		    auto proc = call_msg.cbody().proc;
		    if (proc == 1)
			xdr(val, dec);
		    endCall();

		    accepted_reply ar;
		    ar.verf = { AUTH_NONE, {} };
		    ar.stat = SUCCESS;
		    rpc_msg reply_msg(call_msg.xid, reply_body(std::move(ar)));

		    auto enc = beginReply();
		    xdr(reply_msg, enc);
		    if (proc == 1)
			xdr(val, enc);
		    endReply();

		    if (proc == 2)
			stopping = true;
		}
	    });
    }

    ~SimpleServer()
    {
	thread_.join();
    }

    void stop(shared_ptr<Channel> chan)
    { 
	chan->call(1234, 1, 2, [](XdrSink* xdrs) {}, [](XdrSource* xdrs) {});
    }

    virtual XdrSource* beginCall() = 0;
    virtual void endCall() = 0;
    virtual XdrSink* beginReply() = 0;
    virtual void endReply() = 0;

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

    XdrSource* beginCall() override
    {
	buf_->rewind();
	addrlen_ = addr_.sun_len = sizeof(addr_);
	auto bytes = ::recvfrom(
	    sock_, buf_->buf(), buf_->bufferSize(), 0,
	    reinterpret_cast<sockaddr*>(&addr_), &addrlen_);
	return buf_.get();
    }

    void endCall() override
    {
    }

    XdrSink* beginReply() override
    {
	buf_->rewind();
	return buf_.get();
    }

    void endReply() override
    {
	auto bytes = ::sendto(
	    sock_, buf_->buf(), buf_->pos(), 0,
	    reinterpret_cast<const sockaddr*>(&addr_), addrlen_);
	ASSERT_EQ(buf_->pos(), bytes);
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

    XdrSource* beginCall() override
    {
	return dec_.get();
    }

    void endCall() override
    {
	dec_->endRecord();
    }

    XdrSink* beginReply() override
    {
	return enc_.get();
    }

    void endReply() override
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
    TimeoutChannel()
	: Channel(nullptr)
    {
    }

    unique_ptr<XdrSink> beginCall() override
    {
	return unique_ptr<XdrSink>(new XdrMemory(buf_, sizeof(buf_)));
    }

    void endCall(unique_ptr<XdrSink>&& msg) override
    {
	msg.reset(nullptr);
    }

    unique_ptr<XdrSource> beginReply(
	std::chrono::system_clock::duration timeout) override
    {
	return nullptr;
    }

    void endReply(unique_ptr<XdrSource>&& msg, bool skip)
    {
    }

    void close() override
    {
    }

    uint8_t buf_[1500];
};

TEST_F(ChannelTest, Basic)
{
    TimeoutChannel channel;

    // XXX need an exception type for timeout
    EXPECT_THROW(
	channel.call(1234, 1, 1,
		     [](XdrSink* xdrs) {},
		     [](XdrSource* xdrs) {},
		     chrono::milliseconds(1)),
	RpcError);
}

TEST_F(ChannelTest, LocalChannel)
{
    auto svcreg = make_shared<ServiceRegistry>();
    auto channel = make_shared<LocalChannel>(svcreg);

    // We have no services so any call should return PROG_UNAVAIL
    EXPECT_THROW(simpleCall(channel, 1, 1), ProgramUnavailable);

    // Add a service handler for program 1234, version 2
    auto handler = [](uint32_t proc, XdrSource* xdrin, XdrSink* xdrout)
	{
	    uint32_t v;
	    switch (proc) {
	    case 0:
		return true;
	    case 1:
		xdr(v, xdrin);
		xdr(v, xdrout);
		return true;
	    default:
		return false;
	    }
	};
    svcreg->add(1234, 2, ServiceEntry{handler, {0, 1}});

    // Try calling with the wrong version number and check for
    // PROG_MISMATCH
    EXPECT_THROW(simpleCall(channel, 1, 1), VersionMismatch);

    // Check PROC_UNAVAIL
    EXPECT_THROW(simpleCall(channel, 2, 2), ProcedureUnavailable);

    // Call again with the right version number and procedure
    simpleCall(channel, 2, 1);

    // Unregister our handler and verify that we get PROG_UNAVAIL again
    svcreg->remove(1234, 2);
    EXPECT_THROW(simpleCall(channel, 2, 1), ProgramUnavailable);
}

TEST_F(ChannelTest, LocalManyThreads)
{
    auto svcreg = make_shared<ServiceRegistry>();
    auto cl = std::make_shared<LocalChannel>(svcreg);

    // Add a service handler for program 1234, version 1
    auto handler = [](uint32_t proc, XdrSource* xdrin, XdrSink* xdrout)
	{
	    uint32_t v;
	    switch (proc) {
	    case 0:
		return true;
	    case 1:
		xdr(v, xdrin);
		xdr(v, xdrout);
		return true;
	    default:
		return false;
	    }
	};
    svcreg->add(1234, 1, ServiceEntry{handler, {0, 1}});

    int threadCount = 20;
    int iterations = 200;

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
	threads.push_back(callMany(cl, 1, iterations));

    for (auto& t: threads)
	t.join();
}

TEST_F(ChannelTest, DatagramChannel)
{
    // Make a local socket to listen on
    char tmp[] = "/tmp/rpcTestXXXXX";
    sockaddr_un saddr;
    saddr.sun_len = sizeof(saddr);
    saddr.sun_family = AF_LOCAL;
    strcpy(saddr.sun_path, mktemp(tmp));

    int ssock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    ASSERT_GE(::bind(ssock, reinterpret_cast<const sockaddr*>(&saddr),
		     sizeof(saddr)), 0);

    SimpleDatagramServer server(ssock);

    // Make a suitable address for the channel to receive replies
    char tmp2[] = "/tmp/rpcTestXXXXX";
    sockaddr_un claddr;
    claddr.sun_len = sizeof(claddr);
    claddr.sun_family = AF_LOCAL;
    strcpy(claddr.sun_path, mktemp(tmp2));

    // Bind to our reply address and connect to the server address
    int clsock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    ASSERT_GE(::bind(clsock, reinterpret_cast<const sockaddr*>(&claddr),
		     sizeof(claddr)), 0);
    ASSERT_GE(::connect(clsock, reinterpret_cast<sockaddr*>(&saddr),
			sizeof(saddr)), 0);
    
    // Send a message and check the reply
    auto chan = make_shared<DatagramChannel>(clsock);
    simpleCall(chan, 1, 1);

    server.stop(chan);

    ::unlink(saddr.sun_path);
    ::unlink(claddr.sun_path);
}

TEST_F(ChannelTest, DatagramManyThreads)
{
    // Make a local socket to listen on
    char tmp[] = "/tmp/rpcTestXXXXX";
    sockaddr_un saddr;
    saddr.sun_len = sizeof(saddr);
    saddr.sun_family = AF_LOCAL;
    strcpy(saddr.sun_path, mktemp(tmp));

    int ssock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    ASSERT_GE(::bind(ssock, reinterpret_cast<const sockaddr*>(&saddr),
		     sizeof(saddr)), 0);

    SimpleDatagramServer server(ssock);

    // Make a suitable address for the channel to receive replies
    char tmp2[] = "/tmp/rpcTestXXXXX";
    sockaddr_un claddr;
    claddr.sun_len = sizeof(claddr);
    claddr.sun_family = AF_LOCAL;
    strcpy(claddr.sun_path, mktemp(tmp2));

    // Bind to our reply address and connect to the server address
    int clsock = socket(AF_LOCAL, SOCK_DGRAM, 0);
    ASSERT_GE(::bind(clsock, reinterpret_cast<const sockaddr*>(&claddr),
		     sizeof(claddr)), 0);
    ASSERT_GE(::connect(clsock, reinterpret_cast<sockaddr*>(&saddr),
			sizeof(saddr)), 0);
    auto chan = make_shared<DatagramChannel>(clsock);

    int threadCount = 20;
    int iterations = 200;

    deque<thread> threads;
    for (int i = 0; i < threadCount; i++)
	threads.push_back(callMany(chan, 1, iterations));

    for (auto& t: threads)
	t.join();

    // Ask the server to stop running
    server.stop(chan);

    ASSERT_GE(::unlink(saddr.sun_path), 0);
    ASSERT_GE(::unlink(claddr.sun_path), 0);
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
    simpleCall(chan, 1, 1);

    // Ask the server to stop running
    server.stop(chan);
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
    server.stop(chan);
}

}
