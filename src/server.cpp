#include <cassert>
#include <system_error>

#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <rpc++/errors.h>
#include <rpc++/rpcproto.h>
#include <rpc++/server.h>

using namespace oncrpc;
using namespace oncrpc::_detail;
using namespace std::literals::chrono_literals;

SequenceWindow::SequenceWindow(int size)
    : size_(size),
      largestSeen_(0)
{
}

void
SequenceWindow::update(uint32_t seq)
{
    if (seq > largestSeen_) {
        uint32_t minSeq = seq > size_ - 1 ? seq - size_ + 1 : 0;
        VLOG(3) << "update sequence window: " << seq << " min: " << minSeq;
        while (valid_.size() > 0 && valid_.front() < minSeq)
            valid_.pop_front();
        for (uint32_t i = std::max(minSeq, largestSeen_ + 1); i <= seq; i++)
            valid_.push_back(i);
        largestSeen_ = seq;
    }
}

void
SequenceWindow::reset(uint32_t seq)
{
    // Note: its harmless if our sequence is no longer in the window -
    // some other thread may have called SequenceWindow::update and
    // advanced past us.
    VLOG(3) << "reset sequence window: " << seq;
    auto i = std::find(valid_.begin(), valid_.end(), seq);
    if (i != valid_.end())
	valid_.erase(i);
}

bool
SequenceWindow::valid(uint32_t seq)
{
    for (auto i: valid_)
        if (i == seq)
            return true;
    return false;
}

uint32_t GssClientContext::nextId_ = 0;

GssClientContext::GssClientContext()
    : id_(nextId_++),
      expiry_(std::chrono::system_clock::now() + 5min),
      sequenceWindow_(50)
{
}

GssClientContext::~GssClientContext()
{
    uint32_t min_stat;
    if (context_)
        gss_delete_sec_context(&min_stat, &context_, GSS_C_NO_BUFFER);
}

void
GssClientContext::controlMessage(CallContext& ctx)
{
    auto& cred = ctx.gsscred();
    uint32_t maj_stat, min_stat;

    std::vector<uint8_t> token;
    try {
        ctx.getArgs([&token](XdrSource* xdrs) { xdr(token, xdrs); });
    }
    catch (XdrError& e) {
        ctx.garbageArgs();
        return;
    }

    assert(cred.proc != GssProc::DESTROY);
    gss_buffer_desc inputToken { token.size(), token.data() };
    gss_buffer_desc outputToken { 0, nullptr };
    uint32_t credLifetime;
    maj_stat = gss_accept_sec_context(
        &min_stat,
        &context_,
        GSS_C_NO_CREDENTIAL,
        &inputToken,
        GSS_C_NO_CHANNEL_BINDINGS,
        &clientName_,
        &mechType_,
        &outputToken,
        nullptr,
        &credLifetime,
        nullptr);

    VLOG(2) << "gss_accept_sec_context: major_stat=" << maj_stat
            << ", minor_stat=" << min_stat;
    VLOG(2) << "Replying with " << outputToken.length << " byte token";

    GssInitResult res;
    res.handle.resize(sizeof(uint32_t));
    *reinterpret_cast<uint32_t*>(res.handle.data()) = id_;
    res.major = maj_stat;
    res.minor = min_stat;
    res.sequenceWindow = sequenceWindow_.size();
    res.token.resize(outputToken.length);
    copy_n(static_cast<uint8_t*>(outputToken.value),
           outputToken.length, res.token.begin());
    gss_release_buffer(&min_stat, &outputToken);

    if (maj_stat == GSS_S_COMPLETE) {
        established_ = true;
        auto now = std::chrono::system_clock::now();
        if (credLifetime == GSS_C_INDEFINITE)
            expiry_ = now + 24h;
        else
            expiry_ = now + std::chrono::seconds(credLifetime);
    }

    ctx.sendReply([&res](XdrSink* xdrs) { xdr(res, xdrs); });
}

bool
GssClientContext::verifyCall(CallContext& ctx)
{
    uint8_t buf[512];
    auto& cbody = ctx.msg().cbody();
    XdrMemory xdrcall(buf, sizeof(buf));
    auto xdrs = static_cast<XdrSink*>(&xdrcall);
    xdr(ctx.msg().xid, xdrs);
    xdr(ctx.msg().mtype, xdrs);
    xdr(cbody.rpcvers, xdrs);
    xdr(cbody.prog, xdrs);
    xdr(cbody.vers, xdrs);
    xdr(cbody.proc, xdrs);
    xdr(cbody.cred, xdrs);
    gss_buffer_desc msg { xdrcall.writePos(), buf };
    gss_buffer_desc mic {
        cbody.verf.auth_body.size(),
        const_cast<void*>(reinterpret_cast<const void*>(
            cbody.verf.auth_body.data())) };
    uint32_t maj_stat, min_stat;
    maj_stat = gss_verify_mic(
        &min_stat, context_, &msg, &mic, nullptr);
    if (GSS_ERROR(maj_stat)) {
        VLOG(2) << "xid: " << ctx.msg().xid
                << ": failed to verify message header";
        ctx.authError(RPCSEC_GSS_CREDPROBLEM);
        return false;
    }

    // Silently discard messages with sequence numbers outside the window
    // or which have already been seen
    std::unique_lock<std::mutex> lock(mutex_);
    uint32_t seq = ctx.gsscred().sequence;
    sequenceWindow_.update(seq);
    if (!sequenceWindow_.valid(seq)) {
	VLOG(2) << "out of sequence window xid: " << ctx.msg().xid;
        return false;
    }

    return true;
}

bool
GssClientContext::getVerifier(CallContext& ctx, opaque_auth& verf)
{
    if (!established_) {
        verf = { AUTH_NONE, {} };
        return true;
    }

    auto& cred = ctx.gsscred();

    uint32_t seq;
    if (cred.proc == GssProc::DATA) {
        seq = cred.sequence;
        std::unique_lock<std::mutex> lock(mutex_);
        sequenceWindow_.reset(seq);
    }
    else {
        seq = sequenceWindow_.size();
    }
    VLOG(3) << "sending reply for xid: " << ctx.msg().xid
            << ", sequence: " << seq;

    XdrWord val(seq);
    uint32_t maj_stat, min_stat;
    gss_buffer_desc message{ sizeof(uint32_t), val.data() };
    gss_buffer_desc mic{ 0, nullptr };
    maj_stat = gss_get_mic(
        &min_stat, context_, GSS_C_QOP_DEFAULT, &message, &mic);
    if (GSS_ERROR(maj_stat)) {
        VLOG(2) << "failed to create reply verifier";
        ctx.authError(RPCSEC_GSS_CTXPROBLEM);
        return false;
    }

    verf.flavor = RPCSEC_GSS;
    verf.auth_body.resize(mic.length);
    copy_n(static_cast<uint8_t*>(mic.value), mic.length,
           verf.auth_body.begin());
    gss_release_buffer(&min_stat, &mic);
    return true;
}

ServiceRegistry::ServiceRegistry()
    : clientLifetime_(0s)
{
}

void
ServiceRegistry::add(uint32_t prog, uint32_t vers, Service&& svc)
{
    std::unique_lock<std::mutex> lock(mutex_);
    programs_[prog].insert(vers);
    services_[std::make_pair(prog, vers)] = std::move(svc);
}

void
ServiceRegistry::remove(uint32_t prog, uint32_t vers)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto p = programs_.find(prog);
    assert(p != programs_.end());
    p->second.erase(vers);
    if (p->second.size() == 0)
        programs_.erase(prog);
    services_.erase(std::pair<uint32_t, uint32_t>(prog, vers));
}

const Service
ServiceRegistry::lookup(uint32_t prog, uint32_t vers) const
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto p = services_.find(std::make_pair(prog, vers));
    if (p == services_.end())
        throw ProgramUnavailable(prog);
    return p->second;
}

void
ServiceRegistry::process(CallContext&& ctx)
{
    const rpc_msg& call_msg = ctx.msg();

    if (call_msg.mtype != CALL)
        return;

    VLOG(3) << "xid: " << call_msg.xid << ": received call message";
    if (call_msg.cbody().rpcvers != 2) {
        ctx.rpcMismatch();
        return;
    }

    if (!validateAuth(ctx))
        return;

    try {
        // Simple single-threaded dispatch. To implement more sophisticated
        // dispatch mechanisms, we could wrap the value returned by lookup
        // with a shim which moves the call context to be executed by a thread
        // pool. Alternatively, the application can supply a service handler
        // which could defer execution to some other executor.
        ctx.setService(lookup(ctx.prog(), ctx.vers()));
        ctx();
    }
    catch (ProgramUnavailable& e) {
        // Figure out which error message to use
        std::unique_lock<std::mutex> lock(mutex_);
        auto p = programs_.find(ctx.prog());
        if (p == programs_.end()) {
            lock.unlock();
            ctx.programUnavailable();
        }
        else {
            uint32_t low = ~0U;
            uint32_t high = 0;
            const auto& entry = p->second;
            for (const auto& vers: entry) {
                low = std::min(low, vers);
                high = std::max(high, vers);
            }
            lock.unlock();
            ctx.versionMismatch(low, high);
        }
    }
}

void ServiceRegistry::clearClients()
{
    std::unique_lock<std::mutex> lock(mutex_);
    clients_.clear();
}

bool
ServiceRegistry::validateAuth(CallContext& ctx)
{
    auto& cbody = ctx.msg().cbody();
    auto& cred = ctx.gsscred();

    switch (cbody.cred.flavor) {
    case AUTH_NONE:
    case AUTH_SYS:
        return true;

    case RPCSEC_GSS:
        break;

    default:
        VLOG(2) << "unsupported cred version: " << cbody.cred.flavor;
        ctx.authError(AUTH_BADCRED);
        return false;
    }

    // Decode and sanity check the creds
    try {
        XdrMemory xdrmem(
            const_cast<uint8_t*>(cbody.cred.auth_body.data()),
            cbody.cred.auth_body.size());
        xdr(cred, static_cast<XdrSource*>(&xdrmem));
    }
    catch (XdrError& e) {
        VLOG(2) << "can't decode creds";
        ctx.authError(AUTH_BADCRED);
        return false;
    }
    if (cred.version != 1 ||
        cred.proc < GssProc::DATA ||
        cred.proc > GssProc::DESTROY ||
        cred.service < GssService::NONE ||
        cred.service > GssService::PRIVACY) {
        VLOG(2) << "bad cred values";
        ctx.authError(AUTH_BADCRED);
        return false;
    }
    if (cred.handle.size() > 0 && cred.handle.size() != sizeof(uint32_t)) {
        VLOG(2) << "bad client handle size";
        ctx.authError(AUTH_BADCRED);
        return false;
    }
    if (cred.proc == GssProc::DATA) {
        if (cred.handle.size() != sizeof(uint32_t)) {
            VLOG(2) << "no client handle";
            ctx.authError(AUTH_BADCRED);
            return false;
        }
        if (cred.sequence >= RPCSEC_GSS_MAXSEQ) {
            VLOG(2) << "sequence number overflow";
            ctx.authError(RPCSEC_GSS_CTXPROBLEM);
            return false;
        }
    }

    // Expire old clients
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        std::vector<std::shared_ptr<GssClientContext>> expiredClients;
        for (auto& i: clients_)
            if (i.second->expiry() < now)
                expiredClients.push_back(i.second);
        for (auto client: expiredClients) {
            VLOG(2) << "expiring client " << client->id();
            clients_.erase(client->id());
        }
    }

    // Look up the client if we have a handle
    std::shared_ptr<GssClientContext> client;
    if (cred.handle.size() > 0) {
        uint32_t clientid = *reinterpret_cast<uint32_t*>(cred.handle.data());
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = clients_.find(clientid);
        if (it == clients_.end()) {
            lock.unlock();
            VLOG(2) << "xid: " << ctx.msg().xid
                    << ": can't find client " << clientid;
            ctx.authError(RPCSEC_GSS_CREDPROBLEM);
            return false;
        }
        client = it->second;
    }

    switch (cred.proc) {
    case GssProc::DATA:
        if (cbody.verf.flavor != RPCSEC_GSS) {
            VLOG(2) << "bad verifier flavor";
            ctx.authError(AUTH_BADVERF);
            return false;
        }
        ctx.setClient(client);
        return client->verifyCall(ctx);

    case GssProc::INIT:
        if (client) {
            // Client should pass a null handle
            VLOG(2) << "unexpected client";
            ctx.authError(AUTH_BADCRED);
            return false;
        }
        else {
            std::unique_lock<std::mutex> lock(mutex_);
            client = std::make_shared<GssClientContext>();
            clients_[client->id()] = client;
        }
        // fall through

    case GssProc::CONTINUE_INIT:
        // GssClientContext::controlMessage handles all replies so we return
        // false to prevent further processing of this message.
        ctx.setClient(client);
        client->controlMessage(ctx);
        if (clientLifetime_ > 0s) {
            client->setExpiry(
                std::chrono::system_clock::now() + clientLifetime_);
        }
        return false;

    default:
        assert(false);
    }

    // not reached
    return false;
}
