#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/gss.h>
#include <gtest/gtest.h>

using namespace oncrpc;
using namespace std;

namespace {

[[noreturn]] static void
report_error(gss_OID mech, uint32_t maj, uint32_t min)
{
    uint32_t maj_stat, min_stat;
    uint32_t message_context;
    gss_buffer_desc buf;
    ostringstream ss;

    ss << "GSS-API error: "
       << "major_stat=" << maj
       << ", minor_stat=" << min
       << ": ";

    message_context = 0;
    do {
	maj_stat = gss_display_status(
	    &min_stat, maj, GSS_C_GSS_CODE, GSS_C_NO_OID,
	    &message_context, &buf);
	if (message_context != 0)
	    ss << ", ";
	ss << string((const char*) buf.value, buf.length);
	gss_release_buffer(&min_stat, &buf);
    } while (message_context);
    if (mech) {
	message_context = 0;
	do {
	    maj_stat = gss_display_status(
		&min_stat, min, GSS_C_MECH_CODE, mech,
		&message_context, &buf);
	    if (message_context != 0)
		ss << ", ";
	    ss << string((const char*) buf.value, buf.length);
	    gss_release_buffer(&min_stat, &buf);
	} while (message_context);
    }
    throw RpcError(ss.str());
}

class GssTest: public ::testing::Test
{
public:
    GssTest()
    {
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

class GssServer
{
public:
    /// Procedure 1 is a simple echo service, and procedure
    /// 2 is a stop request. Note, we ignore most of the
    /// RPC protocol for simplicity
    GssServer(int sock)
	: sock_(sock)
    {
	dec_ = std::make_unique<RecordReader>(
	    1500,
	    [=](void* buf, size_t len) {
		auto bytes = ::read(sock_, buf, len);
		if (bytes < 0)
		    throw std::system_error(
			errno, std::system_category());
		return bytes;
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

    void start()
    {
	thread_ = thread(
	    [=]() {
		auto buf = std::make_unique<XdrMemory>(1500);

		bool stopping = false;
		while (!stopping) {
		    rpc_msg call_msg;
		    uint32_t val;
		    rpc_gss_cred_vers_1_t cred;
		    vector<uint8_t> token;

		    try {
			xdr(call_msg, dec_.get());
		    }
		    catch (XdrError& e) {
			break;
		    }
		    auto& cbody = call_msg.cbody();
		    if (cbody.cred.flavor == RPCSEC_GSS) {
			XdrMemory xdrcred(
			    cbody.cred.auth_body.data(),
			    cbody.cred.auth_body.size());
			xdr(cred, static_cast<XdrSource*>(&xdrcred));
		    }
		    if (cbody.proc == 0 && cred.gss_proc != RPCSEC_GSS_DATA) {
			xdr(token, dec_.get());
		    }
		    if (cbody.proc == 1)
			xdr(val, dec_.get());
		    dec_->endRecord();

		    if (cbody.proc == 0 && cred.gss_proc != RPCSEC_GSS_DATA) {
			controlMessage(call_msg.xid, cred, token);
			continue;
		    }

		    // Verify call mic
		    if (cbody.verf.flavor != RPCSEC_GSS) {
			rejected_reply rr;
			rr.stat = AUTH_ERROR;
			rr.auth_error = AUTH_BADVERF;
			rpc_msg reply_msg(
			    call_msg.xid, reply_body(std::move(rr)));
			xdr(reply_msg, enc_.get());
			enc_->pushRecord();
			continue;
		    }

		    if (!verifyCall(call_msg))
			continue;

		    accepted_reply ar;
		    generateVerifier(cred.seq_num, ar.verf);
		    ar.stat = SUCCESS;
		    rpc_msg reply_msg(call_msg.xid, reply_body(std::move(ar)));

		    xdr(reply_msg, enc_.get());
		    if (cbody.proc == 1)
			xdr(val, enc_.get());
		    enc_->pushRecord();

		    if (cbody.proc == 2)
			stopping = true;
		}
	    });
    }

    ~GssServer()
    {
	thread_.join();
    }

    void controlMessage(
	uint32_t xid,
	rpc_gss_cred_vers_1_t& cred,
	vector<uint8_t>& token)
    {
	uint32_t maj_stat, min_stat;

	assert(cred.gss_proc != RPCSEC_GSS_DESTROY);
	gss_buffer_desc inputToken { token.size(), token.data() };
	gss_buffer_desc outputToken { 0, nullptr };
	maj_stat = gss_accept_sec_context(
	    &min_stat,
	    &context_,
	    GSS_C_NO_CREDENTIAL,
	    &inputToken,
	    GSS_C_NO_CHANNEL_BINDINGS,
	    &clientName_,
	    &mechType_,
	    &outputToken,
	    nullptr, nullptr, nullptr);
	if (GSS_ERROR(maj_stat)) {
	    report_error(mechType_, maj_stat, min_stat);
	}

	rpc_gss_init_res res;
	res.handle = { 1, 0, 0, 0 };
	res.gss_major = maj_stat;
	res.gss_minor = min_stat;
	res.seq_window = 1;
	res.gss_token.resize(outputToken.length);
	copy_n(static_cast<uint8_t*>(outputToken.value),
	       outputToken.length, res.gss_token.begin());
	gss_release_buffer(&min_stat, &outputToken);

	accepted_reply ar;
	if (maj_stat == GSS_S_COMPLETE) {
	    XdrWord seq(res.seq_window);
	    gss_buffer_desc message{ sizeof(uint32_t), seq.data() };
	    gss_buffer_desc mic{ 0, nullptr };
	    gss_get_mic(
		&min_stat, context_, GSS_C_QOP_DEFAULT, &message, &mic);
	    ar.verf.flavor = RPCSEC_GSS;
	    ar.verf.auth_body.resize(mic.length);
	    copy_n(static_cast<uint8_t*>(mic.value), mic.length,
		   ar.verf.auth_body.begin());
	    gss_release_buffer(&min_stat, &mic);
	}
	else {
	    ar.verf = { AUTH_NONE, {} };
	}
	ar.stat = SUCCESS;
	rpc_msg reply_msg(xid, reply_body(std::move(ar)));
	xdr(reply_msg, enc_.get());
	xdr(res, enc_.get());
	enc_->pushRecord();
    }

    bool verifyCall(const rpc_msg& call_msg)
    {
	auto cbody = call_msg.cbody();
	auto xdrcall = make_unique<XdrMemory>(512);
	auto xdrs = static_cast<XdrSink*>(xdrcall.get());
	xdr(call_msg.xid, xdrs);
	xdr(call_msg.mtype, xdrs);
	xdr(cbody.rpcvers, xdrs);
	xdr(cbody.prog, xdrs);
	xdr(cbody.vers, xdrs);
	xdr(cbody.proc, xdrs);
	xdr(cbody.cred, xdrs);
	gss_buffer_desc buf {
	    xdrcall->pos(), xdrcall->buf() };
	gss_buffer_desc mic {
	    cbody.verf.auth_body.size(),
		cbody.verf.auth_body.data() };
	uint32_t maj_stat, min_stat;
	maj_stat = gss_verify_mic(
	    &min_stat, context_, &buf, &mic, nullptr);
	if (GSS_ERROR(maj_stat)) {
	    rejected_reply rr;
	    rr.stat = AUTH_ERROR;
	    rr.auth_error = RPCSEC_GSS_CREDPROBLEM;
	    rpc_msg reply_msg(
		call_msg.xid, reply_body(std::move(rr)));
	    xdr(reply_msg, enc_.get());
	    enc_->pushRecord();
	    return false;
	}
	return true;
    }

    void generateVerifier(uint32_t seq, opaque_auth& verf)
    {
	XdrWord seqbuf(seq);
	gss_buffer_desc buf { sizeof(seqbuf), &seqbuf };
	gss_buffer_desc mic;
	uint32_t maj_stat, min_stat;
	maj_stat = gss_get_mic(
	    &min_stat, context_, GSS_C_QOP_DEFAULT, &buf, &mic);
	verf.flavor = RPCSEC_GSS;
	verf.auth_body.resize(mic.length);
	copy_n(
	    static_cast<uint8_t*>(mic.value), mic.length,
	    verf.auth_body.begin());
    }

    void stop(shared_ptr<Channel> chan, shared_ptr<Client> client)
    { 
	chan->call(
	    client.get(), 2, [](XdrSink* xdrs) {}, [](XdrSource* xdrs) {});
    }

    thread thread_;
    int sock_;
    unique_ptr<RecordReader> dec_;
    unique_ptr<RecordWriter> enc_;
    gss_ctx_id_t context_ = GSS_C_NO_CONTEXT;
    gss_name_t clientName_ = GSS_C_NO_NAME;
    gss_OID mechType_ = GSS_C_NO_OID;
};

TEST_F(GssTest, Init)
{
    ::setenv("KRB5_KTNAME", "test.keytab", true);

    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    int ssock = sockpair[0];
    int clsock = sockpair[1];

    GssServer server(ssock);

    // Make a client using RPCSEC_GSS authentication. We assume that
    // there is a host/<gethostname>@DOMAIN entry in the local keytab
    char hbuf[512];
    ::gethostname(hbuf, sizeof(hbuf));
    string principal = "host@";
    principal += hbuf;
    client = make_shared<Client>(
	1234, 1,
	make_unique<GssAuth>(principal, "krb5", RPCSEC_GSS_SVC_NONE));

    // Send a message and check the reply
    auto chan = make_shared<StreamChannel>(clsock);
    simpleCall(chan, client, 1);

    // Ask the server to stop running
    server.stop(chan, client);
}

}

