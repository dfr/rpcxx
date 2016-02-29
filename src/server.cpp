#include <cassert>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <rpc++/cred.h>
#include <rpc++/errors.h>
#include <rpc++/rpcproto.h>
#include <rpc++/server.h>
#include <glog/logging.h>

using namespace oncrpc;
using namespace oncrpc::_detail;
using namespace std::literals::chrono_literals;

#ifndef __APPLE__
thread_local CallContext* CallContext::currentContext_;
#endif

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

GssClientContext::GssClientContext(std::shared_ptr<ServiceRegistry> svcreg)
    : svcreg_(svcreg),
      id_(nextId_++),
      expiry_(std::chrono::system_clock::now() + 5min),
      sequenceWindow_(50)
{
}

GssClientContext::~GssClientContext()
{
    uint32_t min_stat;
    if (clientName_)
	gss_release_name(&min_stat, &clientName_);
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

        lookupCred();
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
	VLOG(2) << "out of sequence window xid: " << ctx.msg().xid
		<< ", sequence: " << seq;
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

    if (VLOG_IS_ON(3)) {
	using namespace std;
	ostringstream ss;
	ss << hex << setw(2) << setfill('0');
	for (auto b: verf.auth_body)
	    ss << int(b);
	VLOG(3) << "reply verifier: " << ss.str();
    }

    return true;
}

std::string GssClientContext::principal() const
{
    uint32_t maj_stat, min_stat;
    gss_buffer_desc buf;
    maj_stat = gss_display_name(&min_stat, clientName_, &buf, nullptr);
    if (GSS_ERROR(maj_stat)) {
        LOG(FATAL) << "failed to export GSS-API name to build cred";
        return "nobody@unknown";
    }
    std::string name(reinterpret_cast<const char*>(buf.value), buf.length);
    gss_release_buffer(&min_stat, &buf);
    return name;
}

void GssClientContext::lookupCred()
{
    haveCred_ = false;

    uint32_t maj_stat, min_stat;
    gss_buffer_desc buf;
    maj_stat = gss_display_name(&min_stat, clientName_, &buf, nullptr);
    if (GSS_ERROR(maj_stat)) {
        LOG(ERROR) << "failed to export GSS-API name to build cred";
        return;
    }
    std::string name(reinterpret_cast<const char*>(buf.value), buf.length);
    gss_release_buffer(&min_stat, &buf);
    auto i = name.rfind('@');
    if (i == std::string::npos) {
        LOG(ERROR) << "expected '@' in principal name";
        return;
    }
    auto user = name.substr(0, i);
    auto realm = name.substr(i + 1);
    VLOG(1) << "looking up credential for user: " << user
            << " in realm: " << realm;

    auto svcreg = svcreg_.lock();
    haveCred_ = svcreg->lookupCred(user, realm, cred_);
}

CallContext::CallContext(
    rpc_msg&& msg, std::unique_ptr<XdrSource> args,
    std::shared_ptr<Channel> chan)
    : size_(args->readSize()),
      msg_(std::move(msg)),
      args_(std::move(args)),
      chan_(chan)
{
}

CallContext::CallContext(CallContext&& other)
    : size_(other.size_),
      msg_(std::move(other.msg_)),
      gsscred_(std::move(other.gsscred_)),
      args_(std::move(other.args_)),
      chan_(std::move(other.chan_)),
      svc_(std::move(other.svc_)),
      client_(std::move(other.client_)),
      credptr_(other.credptr_),
      cred_(std::move(other.cred_))
{
}

CallContext::~CallContext()
{
    if (args_)
        chan_->releaseReceiveBuffer(std::move(args_));
}

const Credential& CallContext::cred()
{
    if (credptr_)
        return *credptr_;
    authError(AUTH_TOOWEAK);
    throw NoReply();
}

void CallContext::lookupCred()
{
    auto& cbody = msg_.cbody();
    switch (cbody.cred.flavor) {
    case AUTH_SYS: {
        XdrMemory xdrmem(
            cbody.cred.auth_body.data(),
            cbody.cred.auth_body.size());
        XdrSource* xdrs = &xdrmem;
        std::uint32_t stamp, uid, gid;
        std::string machinename;
        std::vector<uint32_t> gids;
        xdr(stamp, xdrs);
        xdr(machinename, xdrs);
        xdr(uid, xdrs);
        xdr(gid, xdrs);
        xdr(gids, xdrs);
        cred_ = Credential(uid, gid, std::move(gids));
        credptr_ = &cred_;
        break;
    }

    case RPCSEC_GSS:
        if (client_->haveCred()) {
            credptr_ = &client_->cred();
        }
        break;

    default:
        break;
    }
}

auth_flavor CallContext::flavor()
{
    auto& cbody = msg_.cbody();
    if (cbody.cred.flavor == RPCSEC_GSS) {
        // Hard-wire krb5 flavors
        switch (gsscred_.service) {
        case GssService::NONE:
            return RPCSEC_GSS_KRB5;
        case GssService::INTEGRITY:
            return RPCSEC_GSS_KRB5I;
        case GssService::PRIVACY:
            return RPCSEC_GSS_KRB5P;
        }
    }
    else {
        return cbody.cred.flavor;
    }
}

void CallContext::operator()()
{
#ifdef __APPLE__
    pthread_setspecific(currentContextKey(), this);
#else
    currentContext_ = this;
#endif

    try {
        svc_(std::move(*this));
    }
    catch (XdrError& e) {
        garbageArgs();
    }
    catch (NoReply&) {
        // A service may throw this exception if it has already sent a reply
        // message (e.g. for an authentication failure) and needs to suppress
        // the normal reply mechanism in the rpcgen service stubs
    }

#ifdef __APPLE__
    pthread_setspecific(currentContextKey(), nullptr);
#else
    currentContext_ = nullptr;
#endif
}

void CallContext::getArgs(std::function<void(XdrSource*)> fn)
{
    if (client_) {
        client_->getArgs(fn, gsscred_, args_.get());
    }
    else {
        fn(static_cast<XdrSource*>(args_.get()));
    }
    chan_->releaseReceiveBuffer(std::move(args_));
}

void CallContext::sendReply(std::function<void(XdrSink*)> fn)
{
    accepted_reply areply;
    if (!getVerifier(areply.verf))
        return;
    areply.stat = SUCCESS;
    rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    if (client_) {
        // RFC 2203: 5.3.3.4: If we get an error encoding the reply body,
        // discard the reply.
        if (!client_->sendReply(fn, gsscred_, reply.get())) {
            chan_->releaseSendBuffer(std::move(reply));
            return;
        }
    }
    else {
        try {
            fn(static_cast<XdrSink*>(reply.get()));
        }
        catch (XdrError&) {
            LOG(ERROR) << "xid: " << msg_.xid
                       << ": failed to encode reply body";
            systemError();
            return;
        }
    }
    VLOG(3) << "xid: " << msg_.xid << ": sent reply";
    chan_->sendMessage(std::move(reply));
}

void CallContext::rpcMismatch()
{
    rejected_reply rreply;
    rreply.stat = RPC_MISMATCH;
    rreply.rpc_mismatch.low = 2;
    rreply.rpc_mismatch.high = 2;
    rpc_msg reply_msg(msg_.xid, std::move(rreply));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent RPC_MISMATCH";
    chan_->sendMessage(std::move(reply));
}

void CallContext::garbageArgs()
{
    accepted_reply areply;
    if (!getVerifier(areply.verf))
        return;
    areply.stat = GARBAGE_ARGS;
    rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent GARBAGE_ARGS";
    chan_->sendMessage(std::move(reply));
}

void CallContext::systemError()
{
    accepted_reply areply;
    if (!getVerifier(areply.verf))
        return;
    areply.stat = SYSTEM_ERR;
    rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent SYSTEM_ERR";
    chan_->sendMessage(std::move(reply));
}

void CallContext::procedureUnavailable()
{
    accepted_reply areply;
    if (!getVerifier(areply.verf))
        return;
    areply.stat = PROC_UNAVAIL;
    rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent PROC_UNAVAIL";
    chan_->sendMessage(std::move(reply));
}

void CallContext::programUnavailable()
{
    accepted_reply areply;
    if (!getVerifier(areply.verf))
        return;
    areply.stat = PROG_UNAVAIL;
    rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent PROG_UNAVAIL";
    chan_->sendMessage(std::move(reply));
}

void CallContext::versionMismatch(int low, int high)
{
    accepted_reply areply;
    if (!getVerifier(areply.verf))
        return;
    areply.stat = PROG_MISMATCH;
    auto& mi = areply.mismatch_info;
    mi.low = low;
    mi.high = high;
    rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent PROG_MISMATCH";
    chan_->sendMessage(std::move(reply));
}

void CallContext::authError(auth_stat stat)
{
    rejected_reply rreply;
    rreply.stat = AUTH_ERROR;
    rreply.auth_error = stat;
    rpc_msg reply_msg(msg_.xid, std::move(rreply));
    auto reply = chan_->acquireSendBuffer();
    xdr(reply_msg, reply.get());
    VLOG(3) << "xid: " << msg_.xid << ": sent AUTH_ERROR";
    chan_->sendMessage(std::move(reply));
}

bool CallContext::getVerifier(opaque_auth& verf)
{
    if (client_) {
        return client_->getVerifier(*this, verf);
    }
    else {
        verf = { AUTH_NONE, {} };
        return true;
    }
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
        ctx.lookupCred();
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

void ServiceRegistry::mapCredentials(
    const std::string& realm, std::shared_ptr<CredMapper> map)
{
    credmap_[realm] = std::move(map);
}

bool ServiceRegistry::lookupCred(
    const std::string& user, const std::string& realm, Credential& cred)
{
    auto i = credmap_.find(realm);
    if (i == credmap_.end()) {
        LOG(ERROR) << "Unexpected realm: " << realm;
        return false;
    }
    auto mapper = i->second;
    return mapper->lookupCred(user, cred);
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
            cbody.cred.auth_body.data(),
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
            client = std::make_shared<GssClientContext>(shared_from_this());
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
