#include <iomanip>
#include <iostream>
#include <sstream>

#include <rpc++/channel.h>
#include <rpc++/gss.h>
#include <rpc++/util.h>

using namespace oncrpc;
using namespace oncrpc::_gssdetail;
using namespace std;


GssClient::GssClient(
    uint32_t program, uint32_t version,
    const string& principal,
    const string& mechanism,
    rpc_gss_service_t service)
    : Client(program, version),
      context_(GSS_C_NO_CONTEXT),
      cred_(GSS_C_NO_CREDENTIAL),
      principal_(GSS_C_NO_NAME),
      state_{ 1, RPCSEC_GSS_INIT, 1, service }
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
        reportError(mech_, maj_stat, min_stat);
    }
}

GssClient::~GssClient()
{
    uint32_t min_stat;

    if (context_)
        gss_delete_sec_context(&min_stat, &context_, GSS_C_NO_BUFFER);
    if (cred_)
        gss_release_cred(&min_stat, &cred_);
    if (principal_)
        gss_release_name(&min_stat, &principal_);
}

void
GssClient::validateAuth(Channel* channel)
{
    uint32_t maj_stat, min_stat;

    if (context_ != GSS_C_NO_CONTEXT)
        return;

    VLOG(2) << "Creating GSS-API context";

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
            reportError(mech_, maj_stat, min_stat);
        }
        input_token = {};

        if (output_token.length > 0) {
            VLOG(2) << "Sending "
                    << (state_.gss_proc == RPCSEC_GSS_INIT
                        ? "RPCSEC_GSS_INIT" : "RPCSEC_GSS_CONTINUE_INIT")
                    << " with " << output_token.length << " byte token";
            rpc_gss_init_res res;
            channel->call(
                this, 0,
                [&output_token](XdrSink* xdrs) {
                    // We free the output token as soon as we have written
                    // it to the stream so that we don't have to worry
                    // about it if the channel throws an exception
                    xdrs->putWord(output_token.length);
                    xdrs->putBytes(output_token.value, output_token.length);
                    uint32_t min_stat;
                    gss_release_buffer(&min_stat, &output_token);
                },
                [&res](XdrSource* xdrs) {
                    xdr(res, xdrs);
                });

            if (GSS_ERROR(res.gss_major)) {
                reportError(mech_, res.gss_major, res.gss_minor);
            }

            VLOG(2) << "Received " << res.gss_token.size() << " byte token";
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
        reportError(mech_, maj_stat, min_stat);
    }
    verf_ = {};
    // XXX verify qop here
}

uint32_t
GssClient::processCall(
    uint32_t xid, uint32_t proc, XdrSink* xdrs,
    std::function<void(XdrSink*)> xargs)
{
    int seq = 0;

    if (state_.gss_proc == RPCSEC_GSS_DATA) {
        seq = ++state_.seq_num;
    }

    // More than enough space for the call and cred
    uint8_t callbuf[512];
    uint32_t credlen = 5 * sizeof(XdrWord) + __round(state_.handle.size());
    assert(credlen == XdrSizeof(state_));
    uint32_t calllen;

    XdrMemory xdrcall(callbuf, sizeof(callbuf));
    encodeCall(xid, proc, &xdrcall);
    auto p = xdrcall.writeInline<XdrWord>(2 * sizeof(XdrWord) + credlen);
    //uint8_t* p = nullptr;
    if (p) {
        *p++ = RPCSEC_GSS;
        *p++ = credlen;
        *p++ = state_.gss_ver;
        *p++ = state_.gss_proc;
        *p++ = state_.seq_num;
        *p++ = state_.service;
        auto len = state_.handle.size();
        *p++ = len;
        auto bp = reinterpret_cast<uint8_t*>(p);
        copy_n(state_.handle.data(), len, bp);
        auto pad = __round(len) - len;
        while (pad--)
            *bp++ = 0;
    }
    else {
        xdrcall.putWord(RPCSEC_GSS);
        xdrcall.putWord(credlen);
        xdr(state_, &xdrcall);
    }
    calllen = xdrcall.writePos();

    xdrs->putBytes(callbuf, calllen);
    if (state_.gss_proc == RPCSEC_GSS_DATA) {
        // Create a mic of the RPC header and cred
        uint32_t maj_stat, min_stat;
        gss_buffer_desc mic;
        gss_buffer_desc buf{ calllen, callbuf };
        maj_stat = gss_get_mic(
            &min_stat, context_, GSS_C_QOP_DEFAULT, &buf, &mic);
        if (GSS_ERROR(maj_stat)) {
            reportError(mech_, maj_stat, min_stat);
        }
        xdrs->putWord(RPCSEC_GSS);
        xdrs->putWord(mic.length);
        xdrs->putBytes(mic.value, mic.length);
        gss_release_buffer(&min_stat, &mic);
    }
    else {
        xdrs->putWord(AUTH_NONE);
        xdrs->putWord(0);
    }

    if (state_.gss_proc != RPCSEC_GSS_DATA) {
        xargs(xdrs);
    }
    else {
        encodeBody(context_, mech_, state_.service, seq, xargs, xdrs);
    }

    return seq;
}

bool
GssClient::processReply(
    uint32_t seq,
    accepted_reply& areply,
    XdrSource* xdrs, std::function<void(XdrSource*)> xresults)
{
    auto& verf = areply.verf;

    if (state_.gss_proc != RPCSEC_GSS_DATA) {
        // Save the verifier so that we can verify the sequence window
        // after establishing a context
        if (verf.flavor == RPCSEC_GSS)
            verf_ = verf.auth_body;
        else
            verf_ = {};
        xresults(xdrs);
        return true;
    }
    else {
        // Make sure we read the results before any decision on what
        // to do with the verifier so that we don't get out of phase
        // with the underlying channel
        if (!decodeBody(
                context_, mech_, state_.service, seq, xresults, xdrs))
            return false;
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
        reportError(mech_, maj_stat, min_stat);
    }

    return true;
}
