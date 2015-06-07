#include <iostream>
#include <random>
#include <sstream>

#include <unistd.h>
#include <sys/select.h>

#include <glog/logging.h>

#include <rpc++/client.h>
#include <rpc++/rec.h>
#include <rpc++/server.h>
#include <rpc++/xdr.h>

using namespace oncrpc;

// This would be simpler with thread_local but that isn't supported on
// OS X yet
uint32_t
static nextXid()
{
    static std::mutex mutex;
    static std::random_device rnd;
    std::lock_guard<std::mutex> lock(mutex);
    return rnd();
}

ProgramUnavailable::ProgramUnavailable(uint32_t prog)
    : RpcError([prog]() {
	    std::ostringstream msg;
	    msg << "RPC: program " << prog << " unavailable";
	    return msg.str();
	}()),
      prog_(prog)
{
}

ProcedureUnavailable::ProcedureUnavailable(uint32_t proc)
    : RpcError([proc]() {
	    std::ostringstream msg;
	    msg << "RPC: procedure " << proc << " unavailable";
	    return msg.str();
	}()),
      proc_(proc)
{
}

VersionMismatch::VersionMismatch(uint32_t minver, uint32_t maxver)
    : RpcError([minver, maxver]() {
	    std::ostringstream msg;
	    msg << "RPC: program version mismatch: low version = "
		<< minver << ", high version = " << maxver;
	    return msg.str();
	}()),
      minver_(minver),
      maxver_(maxver)
{
}

GarbageArgs::GarbageArgs()
    : RpcError("RPC: garbage args")
{
}

SystemError::SystemError()
    : RpcError("RPC: remote system error")
{
}

ProtocolMismatch::ProtocolMismatch(uint32_t minver, uint32_t maxver)
    : RpcError([minver, maxver]() {
	    std::ostringstream msg;
	    msg << "RPC: protocol version mismatch: low version = "
		<< minver << ", high version = " << maxver;
	    return msg.str();
	}()),
      minver_(minver),
      maxver_(maxver)
{
}

AuthError::AuthError(auth_stat stat)
    : RpcError([stat]() {
	    static const char* str[] = {
		"AUTH_OK",
		"AUTH_BADCRED",
		"AUTH_REJECTEDCRED",
		"AUTH_BADVERF",
		"AUTH_REJECTEDVERF",
		"AUTH_TOOWEAK",
		"AUTH_INVALIDRESP",
		"AUTH_FAILED",
		"AUTH_KERB",
		"AUTH_TIMEEXPIRE",
		"AUTH_TKT",
		"AUTH_DECODE",
		"AUTH_NET",
		"RPCSEC_GSS_CREDPROBLEM",
		"RPCSEC_GSS_CTXPROBLEM"
	    };
	    std::ostringstream msg;
	    if (stat < AUTH_OK || stat > RPCSEC_GSS_CTXPROBLEM)
		msg << "RPC: unknown auth error: " << int(stat);
	    else
		msg << "RPC: auth error: " << str[stat];
	    return msg.str();
	}()),
      stat_(stat)
{
}

bool
Auth::refresh(auth_stat stat)
{
    return false;
}

void
AuthNone::encode(opaque_auth& cred, opaque_auth& verf)
{
    cred.flavor = AUTH_NONE;
    cred.auth_body.clear();
    verf.flavor = AUTH_NONE;
    verf.auth_body.clear();
}

bool
AuthNone::validate(const opaque_auth& verf)
{
    return true;
}

AuthSys::AuthSys()
{
    char hostname[255];
    gethostname(hostname, 254);
    hostname[254] = '\0';

    std::vector<gid_t> gids;
    gids.resize(getgroups(0, nullptr));
    getgroups(gids.size(), gids.data());

    authsys_parms parms;
    parms.stamp = 0;
    parms.machinename = hostname;
    parms.uid = getuid();
    parms.gid = getgid();
    parms.gids.resize(gids.size());
    std::copy(gids.begin(), gids.end(), parms.gids.begin());

    parms_.resize(512);
    auto xdrs = std::make_unique<XdrMemory>(parms_.data(), 512);
    xdr(parms, static_cast<XdrSink*>(xdrs.get()));
    parms_.resize(xdrs->pos());
}

void
AuthSys::encode(opaque_auth& cred, opaque_auth& verf)
{
    cred.flavor = AUTH_SYS;
    cred.auth_body = parms_;
    verf.flavor = AUTH_NONE;
    verf.auth_body.clear();
}

bool
AuthSys::validate(const opaque_auth& verf)
{
    // We could implement AUTH_SHORT here but there isn't much
    // point since it won't really affect performance and many
    // server implementations don't bother with it.
    return true;
}

std::chrono::seconds Client::maxBackoff(30);

Client::Client(uint32_t prog, uint32_t vers,
	       std::unique_ptr<Auth> auth)
    : prog_(prog),
      vers_(vers),
      xid_(nextXid())
{
    retransmitInterval_ = std::chrono::seconds(1);
    if (auth.get())
	auth_ = std::move(auth);
    else
	auth_ = std::make_unique<AuthNone>();
}

Client::~Client()
{
    assert(pending_.size() == 0);
}

void
Client::call(
    uint32_t proc,
    std::function<void(XdrSink*)> xargs,
    std::function<void(XdrSource*)> xresults,
    std::chrono::system_clock::duration timeout)
{
    int nretries = 0;
    auto retransmitInterval = retransmitInterval_;

call_again:
    call_body cbody;
    cbody.prog = prog_;
    cbody.vers = vers_;
    cbody.proc = proc;
    auth_->encode(cbody.cred, cbody.verf);

    std::unique_lock<std::mutex> lock(mutex_);

    auto xid = xid_++;
    VLOG(2) << "thread: " << std::this_thread::get_id()
	    << " xid: " << xid << ": new call";
    auto i = pending_.emplace(xid, std::move(Transaction()));
    auto& tx = i.first->second;
    rpc_msg call_msg(xid, std::move(cbody));

    auto now = std::chrono::system_clock::now();
    auto maxTime = now + timeout;

    for (;;) {
	// Drop the lock while we transmit
	lock.unlock();
	auto xdrout = beginCall();
	xdr(call_msg, xdrout.get());
	xargs(xdrout.get());
	endCall(std::move(xdrout));
	lock.lock();

	// XXX support oneway

	auto retransmitTime = now + retransmitInterval;
	if (retransmitTime > maxTime)
	    retransmitTime = maxTime;

	// Loop waiting for replies until we either get a matching
	// reply message or we time out
	for (;;) {
	    now = std::chrono::system_clock::now();
	    assert(lock);
	    if (!tx.body) {
		if (running_) {
		    // Someone else is reading replies, wait until they wake
		    // us or until we time out.
		    VLOG(2) << "thread: " << std::this_thread::get_id()
			    << " xid: " << xid
			    << ": waiting for other thread";
		    tx.sleeping = true;
		    auto status = tx.cv->wait_for(lock, retransmitTime - now);
		    tx.sleeping = false;
		    if (status == std::cv_status::no_timeout) {
			if (!tx.body) {
			    // Some other thread woke us up so that we can
			    // take over reading replies
			    continue;
			}
		    }
		}
		else {
		    running_ = true;
		    VLOG(2) << "thread: " << std::this_thread::get_id()
			    << " xid: " << xid
			    << ": waiting for reply";
		    lock.unlock();
		    tx.body = beginReply(retransmitTime - now);
		    lock.lock();
		    if (tx.body)
			xdr(tx.reply, tx.body.get());
		    running_ = false;
		}
	    }

	    // If we timed out or we received a matching reply, we can
	    // stop waiting
	    if (!tx.body || tx.reply.xid == xid) {
		break;
	    }

	    // This may be some other thread's reply. Find them and
	    // wake them up to process it
	    VLOG(2) << "thread: " << std::this_thread::get_id()
		    << " xid: " << tx.reply.xid
		    << ": finding transaction";
	    assert(lock);
	    auto i = pending_.find(tx.reply.xid);
	    if (i != pending_.end()) {
		VLOG(2) << "thread: " << std::this_thread::get_id()
			<< " xid: " << tx.reply.xid
			<< ": waking thread";
		auto& other = i->second;
		other.reply = std::move(tx.reply);
		other.body = std::move(tx.body);
		other.cv->notify_one();
	    }
	    else {
		// If we don't find the transaction, just drop
		// it. This can happen for retransmits.
		VLOG(2) << "thread: " << std::this_thread::get_id()
			<< " xid: " << tx.reply.xid
			<< ": dropping message";
		tx.reply.xid = 0;
		auto msg = std::move(tx.body);
		lock.unlock();
		endReply(std::move(msg), true);
		lock.lock();
	    }
	}

	if (!tx.body) {
	    // We timed out waiting for a reply - retransmit
	    now = std::chrono::system_clock::now();
	    if (now >= maxTime) {
		assert(lock);
		pending_.erase(xid);
		throw RpcError("call timeout");
	    }
	    VLOG(2) << "thread: " << std::this_thread::get_id()
		    << " xid: " << xid
		    << ": retransmitting";
	    nretries++;
	    if (retransmitInterval < maxBackoff)
		retransmitInterval *= 2;
	    continue;
	}

	assert(tx.reply.xid == xid);
	VLOG(2) << "thread: " << std::this_thread::get_id()
		<< " xid: " << xid << ": reply received";

	auto reply_msg = std::move(tx.reply);
	auto xdrin = std::move(tx.body);
	assert(lock);
	pending_.erase(xid);

	// If we have any pending transations, make sure that at lease
	// one thread is awake to read replies.
	if (pending_.size() > 0) {
	    int liveThreads = 0;
	    for (auto& i: pending_) {
		if (!i.second.sleeping)
		    liveThreads++;
	    }
	    if (liveThreads == 0) {
		auto i = pending_.begin();
		VLOG(2) << "thread: " << std::this_thread::get_id()
			<< " waking thread for" << " xid: " << i->first;
		i->second.cv->notify_one();
	    }
	}

	lock.unlock();

	if (reply_msg.mtype == REPLY
	    && reply_msg.rbody().stat == MSG_ACCEPTED
	    && reply_msg.rbody().areply().stat == SUCCESS) {
	    if (!auth_->validate(reply_msg.rbody().areply().verf)) {
		// XXX auth stuff
		assert(false);
	    } else {
		xresults(xdrin.get());
		endReply(std::move(xdrin), false);
		return;
	    }
	}
	else {
	    endReply(std::move(xdrin), false);
	    switch (reply_msg.rbody().stat) {
	    case MSG_ACCEPTED: {
		const auto& areply = reply_msg.rbody().areply();
		switch (areply.stat) {
		case SUCCESS:
		    // Handled above
		    assert(false);

		case PROG_UNAVAIL:
		    throw ProgramUnavailable(prog_);
				
		case PROG_MISMATCH:
		    throw VersionMismatch(
			areply.mismatch_info.low,
			areply.mismatch_info.high);

		case PROC_UNAVAIL:
		    throw ProcedureUnavailable(proc);

		case GARBAGE_ARGS:
		    throw GarbageArgs();

		case SYSTEM_ERR:
		    throw SystemError();

		default:
		    throw RpcError("unknown accept status");
		}
		break;
	    }

	    case MSG_DENIED: {
		auto& rreply = reply_msg.rbody().rreply();
		switch (rreply.stat) {
		case RPC_MISMATCH:
		    throw ProtocolMismatch(
			rreply.rpc_mismatch.low,
			rreply.rpc_mismatch.high);

		case AUTH_ERROR:
		    if (auth_->refresh(rreply.auth_error))
			goto call_again;
		    throw AuthError(rreply.auth_error);

		default:
		    throw RpcError("unknown reject status");
		}
		break;
	    }

	    default:
		throw RpcError("unknown reply status");
	    }
	}
    }
}

LocalClient::LocalClient(
    std::shared_ptr<ServiceRegistry> svcreg,
    uint32_t prog, uint32_t vers, std::unique_ptr<Auth> auth)
    : Client(prog, vers, std::move(auth)),
      bufferSize_(1500),	// XXX size
      svcreg_(svcreg)
{
}

std::unique_ptr<XdrSink>
LocalClient::beginCall()
{
    return std::make_unique<XdrMemory>(bufferSize_);
}

void
LocalClient::endCall(std::unique_ptr<XdrSink>&& tmsg)
{
    std::unique_ptr<XdrMemory> msg(static_cast<XdrMemory*>(tmsg.release()));
    msg->rewind();
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.push_back(std::move(msg));
}

std::unique_ptr<XdrSource>
LocalClient::beginReply(std::chrono::system_clock::duration timeout)
{
    std::unique_ptr<XdrMemory> msg;
    {
	std::unique_lock<std::mutex> lock(mutex_);
	assert(queue_.front());
	msg = std::move(queue_.front());
	queue_.pop_front();
    }
    auto reply = std::make_unique<XdrMemory>(bufferSize_);
    assert(svcreg_->process(msg.get(), reply.get()));
    reply->rewind();
    return std::move(reply);
}

void
LocalClient::endReply(std::unique_ptr<XdrSource>&& msg, bool skip)
{
    std::unique_ptr<XdrMemory> p(static_cast<XdrMemory*>(msg.release()));
    p.reset();
}

void
LocalClient::close()
{
}

SocketClient::SocketClient(
    int sock, uint prog, uint vers, std::unique_ptr<Auth> auth)
    : Client(prog, vers, std::move(auth)),
      sock_(sock)
{
}

bool
SocketClient::waitForReadable(std::chrono::system_clock::duration timeout)
{
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(sock_, &rset);

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(timeout);
    struct timeval tv {
	static_cast<int>(us.count() / 1000000),
	static_cast<int>(us.count() % 1000000)
    };
    
    auto nready = ::select(sock_ + 1, &rset, nullptr, nullptr, &tv);
    if (nready <= 0)
	return false;
    return true;
}

bool
SocketClient::isReadable() const
{
    fd_set rset;
    struct timeval tv { 0, 0 };

    FD_ZERO(&rset);
    FD_SET(sock_, &rset);
    auto nready = ::select(sock_ + 1, &rset, nullptr, nullptr, &tv);
    return nready == 1;
}


bool
SocketClient::isWritable() const
{
    fd_set wset;
    struct timeval tv { 0, 0 };

    FD_ZERO(&wset);
    FD_SET(sock_, &wset);
    auto nready = ::select(sock_ + 1, nullptr, &wset, nullptr, &tv);
    return nready == 1;
}

void
SocketClient::close()
{
    ::close(sock_);
}

DatagramClient::DatagramClient(
    int sock, uint prog, uint vers, std::unique_ptr<Auth> auth)
    : SocketClient(sock, prog, vers, std::move(auth)),
      bufferSize_(1500),
      xdrs_(std::make_unique<XdrMemory>(bufferSize_))
{
}

std::unique_ptr<XdrSink>
DatagramClient::beginCall()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (xdrs_) {
	xdrs_->rewind();
	xdrs_->setSize(xdrs_->bufferSize());
	return std::move(xdrs_);
    }
    else {
	return std::make_unique<XdrMemory>(bufferSize_);
    }
}

void
DatagramClient::endCall(std::unique_ptr<XdrSink>&& msg)
{
    std::unique_ptr<XdrMemory> t(static_cast<XdrMemory*>(msg.release()));
    ::write(sock_, t->buf(), t->pos());
    std::unique_lock<std::mutex> lock(mutex_);
    xdrs_ = std::move(t);
}

std::unique_ptr<XdrSource>
DatagramClient::beginReply(std::chrono::system_clock::duration timeout)
{
    if (!waitForReadable(timeout))
	return nullptr;

    std::unique_ptr<XdrMemory> msg;
    {
	std::unique_lock<std::mutex> lock(mutex_);
	if (xdrs_)
	    msg = std::move(xdrs_);
    }
    if (!msg)
	msg = std::make_unique<XdrMemory>(bufferSize_);

    auto bytes = ::read(sock_, msg->buf(), msg->bufferSize());
    if (bytes <= 0) {
	throw std::system_error(errno, std::system_category());
    }
    msg->rewind();
    msg->setSize(bytes);
    return std::move(msg);
}

void
DatagramClient::endReply(std::unique_ptr<XdrSource>&& msg, bool skip)
{
    std::unique_lock<std::mutex> lock(mutex_);
    xdrs_.reset(static_cast<XdrMemory*>(msg.release()));
}

StreamClient::StreamClient(
    int sock, uint prog, uint vers, std::unique_ptr<Auth> auth)
    : SocketClient(sock, prog, vers, std::move(auth)),
      bufferSize_(1500),
      sender_(
	  std::make_unique<RecordWriter>(
	      bufferSize_,
	      [this](const void* buf, size_t len)
	      {
		  return write(buf, len);
	      }))
{
}

StreamClient::~StreamClient()
{
}

std::unique_ptr<XdrSink>
StreamClient::beginCall()
{
    std::unique_lock<std::mutex> lock(writeMutex_);
    std::unique_ptr<XdrMemory> msg = std::move(sendbuf_);
    if (!msg) {
	msg = std::make_unique<XdrMemory>(bufferSize_);
    }
    return std::move(msg);
}

void
StreamClient::endCall(std::unique_ptr<XdrSink>&& tmsg)
{
    std::unique_lock<std::mutex> lock(writeMutex_);
    std::unique_ptr<XdrMemory> msg;
    msg.reset(static_cast<XdrMemory*>(tmsg.release()));
    sender_->putBytes(msg->buf(), msg->pos());
    sender_->pushRecord();
    msg->rewind();
    sendbuf_ = std::move(msg);
}

static void
readAll(int sock, void* buf, size_t len)
{
    auto p = reinterpret_cast<uint8_t*>(buf);
    auto n = len;

    while (n > 0) {
	auto bytes = ::read(sock, p, n);
	if (bytes < 0)
	    throw std::system_error(errno, std::system_category());
	p += bytes;
	n -= bytes;
    }
}

std::unique_ptr<XdrSource>
StreamClient::beginReply(std::chrono::system_clock::duration timeout)
{
    VLOG(2) << "waiting for reply";
    if (!waitForReadable(timeout))
	return nullptr;
    VLOG(2) << "socket is readable";

    bool done = false;
    std::deque<std::unique_ptr<XdrMemory>> fragments;
    size_t total = 0;
    while (!done) {
	uint8_t recbuf[sizeof(uint32_t)];
	readAll(sock_, recbuf, sizeof(uint32_t));
	uint32_t rec = *reinterpret_cast<const XdrWord*>(recbuf);
	uint32_t reclen = rec & 0x7fffffff;
	done = (rec & (1 << 31)) != 0;
	auto frag = std::make_unique<XdrMemory>(reclen);
	readAll(sock_, frag->buf(), reclen);
	fragments.push_back(std::move(frag));
	total += reclen;
    }

    if (fragments.size() == 1) {
	return std::move(fragments[0]);
    }

    // We could create a new XdrSource here to process the queue but
    // most cases should take the simpler single-fragment path
    auto msg = std::make_unique<XdrMemory>(total);
    auto p = msg->buf();
    for (const auto& frag: fragments) {
	auto n = frag->bufferSize();
	std::copy_n(frag->buf(), n, p);
	p += n;
    }
    return std::move(msg);
}

void
StreamClient::endReply(std::unique_ptr<XdrSource>&& tmsg, bool skip)
{
    std::unique_ptr<XdrMemory> msg;
    msg.reset(static_cast<XdrMemory*>(tmsg.release()));
}

ptrdiff_t
StreamClient::write(const void* buf, size_t len)
{
    // This will always be called via endCall with writeMutex_ held.
    auto p = reinterpret_cast<const uint8_t*>(buf);
    auto n = len;

    VLOG(2) << "writing " << len << " bytes to socket";
    while (n > 0) {
	auto bytes = ::write(sock_, p, len);
	if (bytes < 0)
	    throw std::system_error(errno, std::system_category());
	p += bytes;
	n -= bytes;
    }
    return len;
}

