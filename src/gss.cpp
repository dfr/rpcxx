#include <iomanip>
#include <iostream>
#include <sstream>

#include <CoreFoundation/CoreFoundation.h>

#include <rpc++/channel.h>
#include <rpc++/gss.h>
#include <rpc++/util.h>

using namespace oncrpc;
using namespace std;

[[noreturn]] static void
report_error(gss_OID mech, uint32_t maj, uint32_t min)
{
    {
	CFErrorRef err = GSSCreateError(mech, maj, min);
	CFStringRef str = CFErrorCopyDescription(err);
	char buf[512];
	CFStringGetCString(str, buf, 512, kCFStringEncodingUTF8);
	cout << buf << endl;
    }

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

GssAuth::GssAuth(
    const string& principal,
    const string& mechanism,
    rpc_gss_service_t service)
    : context_(GSS_C_NO_CONTEXT),
      cred_(GSS_C_NO_CREDENTIAL),
      principal_(GSS_C_NO_NAME),
      state_{ RPCSEC_GSS_INIT, 1, service }
{
    static gss_OID_desc krb5_desc =
	{9, (void *)"\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"};
    // Only Kerberos V5 supported
    if (mechanism != "krb5") {
	throw RpcError(
	    string("Unsupported GSS-API mechanism: ") + mechanism);
    }
    mech_ = &krb5_desc;

    // Get the GSS-API name for the given principal
    uint32_t maj_stat, min_stat;
    gss_buffer_desc name_desc {
	principal.size(), const_cast<char*>(&principal[0]) };
    maj_stat = gss_import_name(
	&min_stat, &name_desc, GSS_C_NT_HOSTBASED_SERVICE, &principal_);
    if (GSS_ERROR(maj_stat)) {
	report_error(mech_, maj_stat, min_stat);
    }
}

void
GssAuth::init(Client* client, Channel* channel)
{
    uint32_t maj_stat, min_stat;

    if (context_ != GSS_C_NO_CONTEXT)
	return;

    // Establish the GSS-API context with the remote service
    vector<uint8_t> input_token;
    gss_buffer_desc output_token { 0, nullptr };
    while (state_.gss_proc != RPCSEC_GSS_DATA || input_token.size() > 0) {
	gss_buffer_desc tmp {input_token.size(), input_token.data() };
	maj_stat = gss_init_sec_context(
	    &min_stat,
	    cred_,
	    &context_,
	    principal_,
	    mech_,
	    GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG|GSS_C_INTEG_FLAG,
	    0,
	    GSS_C_NO_CHANNEL_BINDINGS,
	    &tmp,
	    nullptr,
	    &output_token,
	    nullptr,
	    nullptr);
	if (maj_stat != GSS_S_COMPLETE &&
	    maj_stat != GSS_S_CONTINUE_NEEDED) {
	    report_error(mech_, maj_stat, min_stat);
	}
	input_token = {};

	if (output_token.length > 0) {
	    rpc_gss_init_res res;
	    channel->call(
		client, 0,
		[&output_token](XdrSink* xdrs) {
		    // We free the output token as soon as we have written
		    // it to the stream so that we don't have to worry
		    // about it if the channel throws an exception
		    uint32_t len = output_token.length;
		    xdr(len, xdrs);
		    xdrs->putBytes(
			static_cast<uint8_t*>(output_token.value), len);
		    uint32_t min_stat;
		    gss_release_buffer(&min_stat, &output_token);
		},
		[&res](XdrSource* xdrs) {
		    xdr(res, xdrs);
		});

	    if (GSS_ERROR(res.gss_major)) {
		report_error(mech_, res.gss_major, res.gss_minor);
	    }

	    input_token = move(res.gss_token);
	    state_.handle = move(res.handle);
	    seq_window_ = res.seq_window;

	    if (res.gss_major == GSS_S_COMPLETE) {
		state_.gss_proc = RPCSEC_GSS_DATA;
	    }
	    else {
		state_.gss_proc = RPCSEC_GSS_CONTINUE_INIT;
	    }
	}
    }

    // We saved the RPC reply verf field in validate below. Use it to
    // verify the sequence window returned by the server
    XdrWord seq(seq_window_);
    gss_qop_t qop;
    gss_buffer_desc message{ sizeof(uint32_t), seq.data() };
    gss_buffer_desc token{ verf_.size(), verf_.data() };
    maj_stat = gss_verify_mic(
	&min_stat, context_, &message, &token, &qop);
    if (GSS_ERROR(maj_stat)) {
	report_error(mech_, maj_stat, min_stat);
    }
    verf_ = {};
    // XXX verify qop here
}

uint32_t
GssAuth::encode(
    uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc, XdrSink* xdrs)
{
    uint8_t credbuf[400];
    uint32_t credlen;
    int seq = 0;

    if (state_.gss_proc == RPCSEC_GSS_DATA) {
	seq = ++state_.seq_num;
    }

    {
	XdrMemory xdrcred(credbuf, sizeof(credbuf));
	xdr(state_, static_cast<XdrSink*>(&xdrcred));
	credlen = xdrcred.pos();
    }

    // More than enough space for the call and cred
    uint8_t callbuf[512];
    uint32_t calllen;

    {
	XdrMemory xdrcall(callbuf, sizeof(callbuf));
	xdrcall.putWord(xid);
	xdrcall.putWord(CALL);
	xdrcall.putWord(2);
	xdrcall.putWord(prog);
	xdrcall.putWord(vers);
	xdrcall.putWord(proc);
	xdrcall.putWord(RPCSEC_GSS);
	xdrcall.putWord(credlen);
	xdrcall.putBytes(credbuf, credlen);
	calllen = xdrcall.pos();
    }

    xdrs->putBytes(callbuf, calllen);
    if (state_.gss_proc == RPCSEC_GSS_DATA) {
	// Create a mic of the RPC header and cred
	uint32_t maj_stat, min_stat;
	gss_buffer_desc mic;
	gss_buffer_desc buf{ calllen, callbuf };
	maj_stat = gss_get_mic(
	    &min_stat, context_, GSS_C_QOP_DEFAULT, &buf, &mic);
	if (GSS_ERROR(maj_stat)) {
	    report_error(mech_, maj_stat, min_stat);
	}
	xdrs->putWord(RPCSEC_GSS);
	xdrs->putWord(mic.length);
	xdrs->putBytes(static_cast<uint8_t*>(mic.value), mic.length);
	gss_release_buffer(&min_stat, &mic);
    }
    else {
	xdrs->putWord(AUTH_NONE);
	xdrs->putWord(0);
    }
    return seq;
}

bool
GssAuth::validate(uint32_t seq, opaque_auth& verf)
{
    // Save the verifier so that we can verify the sequence window
    // after establishing a context
    if (state_.gss_proc != RPCSEC_GSS_DATA) {
	if (verf.flavor == RPCSEC_GSS)
	    verf_ = verf.auth_body;
	else
	    verf_ = {};
	return true;
    }

    if (verf.flavor != RPCSEC_GSS)
	return false;

    XdrWord seqbuf(seq);
    gss_buffer_desc buf { sizeof(seqbuf), &seqbuf };
    gss_buffer_desc mic { verf.auth_body.size(), verf.auth_body.data() };
    uint32_t maj_stat, min_stat;
    maj_stat = gss_verify_mic(
	&min_stat, context_, &buf, &mic, nullptr);
    if (GSS_ERROR(maj_stat)) {
	if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
	    // XXX destroy context and re-init
	}
	report_error(mech_, maj_stat, min_stat);
    }

    return true;
}

bool
GssAuth::refresh(auth_stat stat)
{
    return true;
}

