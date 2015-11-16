#include <iomanip>
#include <iostream>
#include <sstream>

#include <rpc++/channel.h>
#include <rpc++/errors.h>
#include <rpc++/gss.h>
#include <glog/logging.h>

using namespace oncrpc;
using namespace oncrpc::_detail;
using namespace std;

void badSequence(uint32_t seq, uint32_t checkSeq)
{
    VLOG(1) << "Bad sequence number in reply:"
            << " expected " << seq
            << " received " << checkSeq;
}

namespace {

// Special client which is used during context initialisation. Having it
// separate simplfies the logic in GssClient and makes it easier to lock.
class ContextClient: public Client
{
public:
    ContextClient(
        uint32_t program, uint32_t version, const GssCred& cred)
        : Client(program, version),
          cred_(cred)
    {
    }

    bool processCall(
        uint32_t xid, int gen, uint32_t proc, XdrSink* xdrs,
        std::function<void(XdrSink*)> xargs, Protection prot, uint32_t& seq) override
    {
        uint32_t credlen = 5 * sizeof(XdrWord) + __round(cred_.handle.size());
        XdrMemory xdrcred(credlen);
        xdr(cred_, static_cast<XdrSink*>(&xdrcred));

        encodeCall(xid, proc, xdrs);
        xdrs->putWord(RPCSEC_GSS);
        xdrs->putWord(credlen);
        xdrs->putBytes(xdrcred.buf(), credlen);
        xdrs->putWord(AUTH_NONE);
        xdrs->putWord(0);
        xargs(xdrs);
        seq = 0;
        return true;
    }

    bool processReply(
        uint32_t seq, int gen, accepted_reply& areply,
        XdrSource* xdrs, std::function<void(XdrSource*)> xresults,
        Protection prot) override
    {
        verf_ = move(areply.verf.auth_body);
        xresults(xdrs);
        return true;
    }

    const GssCred& cred_;
    vector<uint8_t> verf_;
};

}

GssClient::GssClient(
    uint32_t program, uint32_t version,
    const string& principal,
    const string& mechanism,
    GssService service)
    : Client(program, version),
      context_(GSS_C_NO_CONTEXT),
      cred_(GSS_C_NO_CREDENTIAL),
      principal_(GSS_C_NO_NAME),
      sequence_(1),
      established_(false),
      defaultService_(service)
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

GssClient::GssClient(uint32_t program, uint32_t version,
        const std::string& initiator,
        const std::string& principal,
        const std::string& mechanism,
        GssService service)
    : GssClient(program, version, principal, mechanism, service)
{
    // Get the GSS-API name for the initiator
    uint32_t maj_stat, min_stat;
    gss_name_t name;

    gss_buffer_desc name_desc {
        initiator.size(), const_cast<char*>(&initiator[0]) };
    maj_stat = gss_import_name(
        &min_stat, &name_desc, GSS_C_NT_USER_NAME, &name);
    if (GSS_ERROR(maj_stat)) {
        reportError(mech_, maj_stat, min_stat);
    }

    maj_stat = gss_acquire_cred(
        &min_stat,
        name,
        GSS_C_INDEFINITE,
        GSS_C_NO_OID_SET,
        GSS_C_INITIATE,
        &cred_,
        NULL,
        NULL);
    if (GSS_ERROR(maj_stat))
        reportError(mech_, maj_stat, min_stat);
    gss_release_name(&min_stat, &name);
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
GssClient::setService(GssService service)
{
    defaultService_ = service;
}

int
GssClient::validateAuth(Channel* channel, bool revalidate)
{
    uint32_t maj_stat, min_stat;

    if (established_) {
        // re-check after taking the lock
        std::unique_lock<std::mutex> lock(mutex_);
        if (established_)
            return generation_;
    }

    if (!revalidate)
        return 0;

    std::unique_lock<std::mutex> lock(mutex_);

    if (established_) {
        return generation_;
    }

    generation_++;
    VLOG(2) << "Creating GSS-API context, generation " << generation_;

    // Establish the GSS-API context with the remote service
    vector<uint8_t> inputToken;
    gss_buffer_desc outputToken { 0, nullptr };
    GssCred cred{ 1, GssProc::INIT, 1, GssService::NONE, {}};
    ContextClient client(program_, version_, cred);
    while (!established_ || inputToken.size() > 0) {
        gss_buffer_desc tmp {inputToken.size(), inputToken.data() };
        uint32_t flags;
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
            &outputToken,
            &flags,
            nullptr);
        assert(!(flags & (GSS_C_SEQUENCE_FLAG|GSS_C_REPLAY_FLAG)));
        if (maj_stat != GSS_S_COMPLETE &&
            maj_stat != GSS_S_CONTINUE_NEEDED) {
            reportError(mech_, maj_stat, min_stat);
        }
        inputToken = {};

        if (outputToken.length > 0) {
            VLOG(2) << "Sending "
                    << (cred.proc == GssProc::INIT
                        ? "GssProc::INIT" : "GssProc::CONTINUE_INIT")
                    << " with " << outputToken.length << " byte token";
            GssInitResult res;
            channel->call(
                &client, 0,
                [&outputToken](XdrSink* xdrs) {
                    // We free the output token as soon as we have written
                    // it to the stream so that we don't have to worry
                    // about it if the channel throws an exception
                    xdrs->putWord(outputToken.length);
                    xdrs->putBytes(outputToken.value, outputToken.length);
                    uint32_t min_stat;
                    gss_release_buffer(&min_stat, &outputToken);
                },
                [&res](XdrSource* xdrs) {
                    xdr(res, xdrs);
                });

            if (GSS_ERROR(res.major)) {
                reportError(mech_, res.major, res.minor);
            }

            VLOG(2) << "Received " << res.token.size() << " byte token";
            inputToken = move(res.token);
            handle_ = move(res.handle);
            cred.handle = handle_;
            sequenceWindow_ = res.sequenceWindow;
            inflightCalls_ = 0;

            if (res.major == GSS_S_COMPLETE) {
                established_ = true;
            }
            else {
                cred.proc = GssProc::CONTINUE_INIT;
            }
        }
    }

    // We saved the RPC reply verf field in the ContextClient. Use it to
    // verify the sequence window returned by the server
    XdrWord seq(sequenceWindow_);
    gss_qop_t qop;
    gss_buffer_desc message{ sizeof(uint32_t), seq.data() };
    gss_buffer_desc token{ client.verf_.size(), client.verf_.data() };
    maj_stat = gss_verify_mic(
        &min_stat, context_, &message, &token, &qop);
    if (GSS_ERROR(maj_stat)) {
        reportError(mech_, maj_stat, min_stat);
    }
    // XXX verify qop here

    VLOG(2) << "Finished establishing context, window size "
            << sequenceWindow_;
    return generation_;
}

bool
GssClient::processCall(
    uint32_t xid, int gen, uint32_t proc, XdrSink* xdrs,
    std::function<void(XdrSink*)> xargs, Protection prot,
    uint32_t& seq)
{
    seq = 0;

    std::unique_lock<std::mutex> lock(mutex_);
    if (!established_ || gen != generation_) {
        // Someone else has deleted the context so we need to re-validate
        VLOG(2) << "Can't process call: context deleted";
        return false;
    }
    while (inflightCalls_ >= sequenceWindow_) {
        VLOG(2) << "Waiting for a slot in the sequence window";
        cv_.wait(lock);
    }
    inflightCalls_++;
    seq = ++sequence_;
    auto service = getService(prot);
    VLOG(3) << "sending message service: " << int(service)
            << ", gen: " << gen << ", sequence: " << seq;

    // More than enough space for the call and cred
    uint8_t callbuf[512];
    uint32_t credlen = 5 * sizeof(XdrWord) + __round(handle_.size());
    uint32_t calllen;

    XdrMemory xdrcall(callbuf, sizeof(callbuf));
    encodeCall(xid, proc, &xdrcall);
    auto p = xdrcall.writeInline<XdrWord>(2 * sizeof(XdrWord) + credlen);
    //uint8_t* p = nullptr;
    if (p) {
        *p++ = RPCSEC_GSS;
        *p++ = credlen;
        *p++ = 1;
        *p++ = uint32_t(GssProc::DATA);
        *p++ = seq;
        *p++ = uint32_t(service);
        auto len = handle_.size();
        *p++ = len;
        auto bp = reinterpret_cast<uint8_t*>(p);
        copy_n(handle_.data(), len, bp);
        auto pad = __round(len) - len;
        while (pad--)
            *bp++ = 0;
    }
    else {
        xdrcall.putWord(RPCSEC_GSS);
        xdrcall.putWord(credlen);
        xdr(GssCred{ 1, GssProc::DATA, seq, service, handle_}, &xdrcall);
    }
    calllen = xdrcall.writePos();

    xdrs->putBytes(callbuf, calllen);
    // Create a mic of the RPC header and cred
    uint32_t maj_stat, min_stat;
    gss_buffer_desc mic;
    gss_buffer_desc buf{ calllen, callbuf };
    maj_stat = gss_get_mic(
        &min_stat, context_, GSS_C_QOP_DEFAULT, &buf, &mic);
    lock.unlock();
    if (GSS_ERROR(maj_stat)) {
        reportError(mech_, maj_stat, min_stat);
    }
    xdrs->putWord(RPCSEC_GSS);
    xdrs->putWord(mic.length);
    xdrs->putBytes(mic.value, mic.length);
    gss_release_buffer(&min_stat, &mic);

    encodeBody(context_, mech_, service, seq, xargs, xdrs);

    return seq;
}

bool
GssClient::processReply(
    uint32_t seq, int gen, accepted_reply& areply,
    XdrSource* xdrs, std::function<void(XdrSource*)> xresults,
    Protection prot)
{
    auto& verf = areply.verf;

    std::unique_lock<std::mutex> lock(mutex_);
    if (gen != generation_ || !established_) {
        // Someone else has deleted the context so we need to re-validate
        VLOG(2) << "Can't process reply: context deleted";
        return false;
    }

    inflightCalls_--;
    cv_.notify_one();
    lock.unlock();

    // Make sure we read the results before any decision on what
    // to do with the verifier so that we don't get out of phase
    // with the underlying channel
    if (!decodeBody(
            context_, mech_, getService(prot), seq, xresults, xdrs))
        return false;

    if (verf.flavor != RPCSEC_GSS)
        return false;

    VLOG(3) << "verifying reply for gen: " << gen << ", sequence: " << seq;
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

bool
GssClient::authError(int gen, int stat)
{
    if (stat == RPCSEC_GSS_CREDPROBLEM || stat == RPCSEC_GSS_CTXPROBLEM) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (gen != generation_) {
            VLOG(2) << "Auth error: context already deleted";
        }
        else {
            VLOG(2) << "Auth error: deleting context";
            uint32_t min_stat;
            if (context_) {
                gss_delete_sec_context(&min_stat, &context_, GSS_C_NO_BUFFER);
                context_ = GSS_C_NO_CONTEXT;
            }
            established_ = false;
            sequence_ = 1;
            handle_ = {};
        }
        return true;
    }
    return false;
}
