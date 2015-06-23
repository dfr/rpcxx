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

    bool testService(uint32_t proc, XdrSource* xdrin, XdrSink* xdrout)
    {
	switch (proc) {
	case 0:
	    return true;

	case 1:
	    uint32_t val;
	    xdr(val, xdrin);
	    xdr(val, xdrout);
	    return true;

	default:
	    return false;
	}
    }

    void addTestService()
    {
	svcreg->add(
	    1234, 1,
	    ServiceEntry{
		bind(&ServerTest::testService, this, _1, _2, _3),
		{0, 1}});
    }

    void checkReply(uint32_t prog, uint32_t vers, uint32_t proc,
		    accept_stat expectedStat,
		    const vector<uint8_t>& args,
		    const vector<uint8_t>& res,
		    rpc_msg* reply_msg = nullptr)
    {
	uint8_t call[1500];
	uint8_t reply[1500];
	auto xdrout = unique_ptr<XdrMemory>(
	    new XdrMemory(call, sizeof(call)));
	auto xdrin = unique_ptr<XdrMemory>(
	    new XdrMemory(reply, sizeof(reply)));

	call_body cbody;
	cbody.prog = prog;
	cbody.vers = vers;
	cbody.proc = proc;
	rpc_msg msg(1, std::move(cbody));
	xdr(msg, static_cast<XdrSink*>(xdrout.get()));
	xdrout->putBytes(args.data(), args.size());
	xdrout->rewind();

	EXPECT_TRUE(svcreg->process(xdrout.get(), xdrin.get()));
	xdrin->rewind();
	xdr(msg, static_cast<XdrSource*>(xdrin.get()));
	EXPECT_EQ(msg.xid, 1);
	EXPECT_EQ(msg.mtype, REPLY);
	EXPECT_EQ(msg.rbody().stat, MSG_ACCEPTED);
	EXPECT_EQ(msg.rbody().areply().stat, expectedStat);

	vector<uint8_t> t;
	t.resize(res.size());
	xdrin->getBytes(t.data(), t.size());
	EXPECT_EQ(res, t);

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
    uint8_t call[1500];
    uint8_t reply[1500];
    auto xdrout = unique_ptr<XdrMemory>(
	new XdrMemory(call, sizeof(call)));
    auto xdrin = unique_ptr<XdrMemory>(
	new XdrMemory(reply, sizeof(reply)));

    // Check RPC_MISMATCH is generated for rpcvers other than 2
    call_body cbody;
    cbody.rpcvers = 3;
    cbody.prog = 1234;
    cbody.vers = 0;
    cbody.proc = 0;
    rpc_msg msg(1, std::move(cbody));
    xdr(msg, static_cast<XdrSink*>(xdrout.get()));
    xdrout->rewind();

    EXPECT_TRUE(svcreg->process(xdrout.get(), xdrin.get()));
    xdrin->rewind();
    xdr(msg, static_cast<XdrSource*>(xdrin.get()));
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

    auto connreg = make_shared<ConnectionRegistry>();
    connreg->add(make_shared<StreamConnection>(sockpair[1], 32768, svcreg));
    thread server([connreg]() { connreg->run(); });

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
 
    auto connreg = make_shared<ConnectionRegistry>();
    connreg->add(make_shared<ListenConnection>(lsock, 32768, svcreg));
    thread server([connreg]() { connreg->run(); });

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
    connreg->stop();
    chan->close();
    chan.reset();
    server.join();

    EXPECT_GE(::unlink(sun.sun_path), 0);
}

}
