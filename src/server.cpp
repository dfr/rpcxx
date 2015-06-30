#include <cassert>
#include <system_error>

#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <rpc++/rpcproto.h>
#include <rpc++/server.h>

using namespace oncrpc;

void
ServiceRegistry::add(
	uint32_t prog, uint32_t vers, ServiceEntry&& entry)
{
    programs_[prog].insert(vers);
    services_[std::make_pair(prog, vers)] = entry;
}

void
ServiceRegistry::remove(uint32_t prog, uint32_t vers)
{
    auto p = programs_.find(prog);
    assert(p != programs_.end());
    p->second.erase(vers);
    if (p->second.size() == 0)
	programs_.erase(prog);
    services_.erase(std::pair<uint32_t, uint32_t>(prog, vers));
}

const ServiceEntry*
ServiceRegistry::lookup(uint32_t prog, uint32_t vers) const
{
    auto p = services_.find(std::make_pair(prog, vers));
    if (p == services_.end())
	return nullptr;
    return &p->second;
}

bool
ServiceRegistry::process(XdrSource* xdrin, XdrSink* xdrout)
{
    rpc_msg call_msg;

    try {
	xdr(call_msg, xdrin);
    }
    catch (XdrError&) {
	return false;
    }

    if (call_msg.mtype != CALL)
	return false;

    if (call_msg.cbody().rpcvers != 2) {
	rejected_reply rreply;
	rreply.stat = RPC_MISMATCH;
	rreply.rpc_mismatch.low = 2;
	rreply.rpc_mismatch.high = 2;
	rpc_msg reply_msg(call_msg.xid, std::move(rreply));
	xdr(reply_msg, xdrout);
	return true;
    }

    // XXX validate auth

    accepted_reply areply;

    areply.verf = { AUTH_NONE, {} };
    auto p = services_.find(
	std::make_pair(call_msg.cbody().prog, call_msg.cbody().vers));
    if (p != services_.end()) {
	auto proc = call_msg.cbody().proc;
	const auto& entry = p->second;
	if (entry.procs.find(proc) != entry.procs.end()) {
	    areply.stat = SUCCESS;
	    rpc_msg reply_msg(call_msg.xid, reply_body(std::move(areply)));
	    xdr(reply_msg, xdrout);
	    entry.handler(proc, xdrin, xdrout);
	}
	else {
	    areply.stat = PROC_UNAVAIL;
	    rpc_msg reply_msg(call_msg.xid, reply_body(std::move(areply)));
	    xdr(reply_msg, xdrout);
	}
    }
    else {
	// Figure out which error message to use
	auto p = programs_.find(call_msg.cbody().prog);
	if (p == programs_.end()) {
	    areply.stat = PROG_UNAVAIL;
	}
	else {
	    areply.stat = PROG_MISMATCH;
	    auto& mi = areply.mismatch_info;
	    mi.low = ~0U;
	    mi.high = 0;
	    const auto& entry = p->second;
	    for (const auto& vers: entry) {
		mi.low = std::min(mi.low, vers);
		mi.high = std::max(mi.high, vers);
	    }
	}
	rpc_msg reply_msg(call_msg.xid, reply_body(std::move(areply)));
	xdr(reply_msg, xdrout);
    }
    return true;
}

void
ConnectionRegistry::add(std::shared_ptr<Connection> conn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    conns_[conn->sock()] = conn;
}

void
ConnectionRegistry::remove(std::shared_ptr<Connection> conn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    conns_.erase(conn->sock());
}

void
ConnectionRegistry::run()
{
    mutex_.lock();
    while (conns_.size() > 0 && !stopping_) {
	fd_set rset;
	int maxfd = 0;
	FD_ZERO(&rset);
	for (const auto& i: conns_) {
	    int fd = i.first;
	    maxfd = std::max(maxfd, fd);
	    FD_SET(fd, &rset);
	}
	mutex_.unlock();
	auto nready = ::select(maxfd + 1, &rset, nullptr, nullptr, nullptr);
	if (nready < 0) {
	    throw std::system_error(errno, std::system_category());
	}

	if (nready == 0)
	    continue;

	mutex_.lock();
	std::vector<std::shared_ptr<Connection>> ready;
	for (const auto& i: conns_) {
	    if (FD_ISSET(i.first, &rset)) {
		ready.push_back(i.second);
	    }
	}
	mutex_.unlock();

	for (auto conn: ready) {
	    if (!conn->onReadable(this))
		remove(conn);
	}

	mutex_.lock();
    }
    mutex_.unlock();
}

Connection::Connection(
    int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg)
    : sock_(sock), bufferSize_(bufferSize), svcreg_(svcreg)
{
}

Connection::~Connection()
{
    ::close(sock_);
}

DatagramConnection::DatagramConnection(
    int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg)
    : Connection(sock, bufferSize, svcreg),
      receivebuf_(bufferSize),
      sendbuf_(bufferSize),
      dec_(std::make_unique<XdrMemory>(receivebuf_.data(), bufferSize)),
      enc_(std::make_unique<XdrMemory>(sendbuf_.data(), bufferSize))
{
}

bool
DatagramConnection::onReadable(ConnectionRegistry*)
{
    enc_->rewind();
    dec_->rewind();
    auto bytes = ::read(sock_, receivebuf_.data(), bufferSize_);
    if (bytes < 0)
	return false;
    dec_->setReadSize(bytes);
    if (svcreg_->process(dec_.get(), enc_.get()))
	if (::write(sock_, sendbuf_.data(), enc_->writePos()) < 0)
	    return false;
    return true;
}

StreamConnection::StreamConnection(
    int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg)
    : Connection(sock, bufferSize, svcreg),
      dec_(std::make_unique<RecordReader>(
	       bufferSize,
	       [this](void* buf, size_t len) {
		   return ::read(sock_, buf, len);
	       })),
      enc_(std::make_unique<RecordWriter>(
	       bufferSize,
	       [this](const void* buf, size_t len) {
		   const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
		   size_t n = len;
		   while (n > 0) {
		       auto bytes = ::write(sock_, p, len);
		       if (bytes < 0)
			   throw std::system_error(errno, std::system_category());
		       p += bytes;
		       n -= bytes;
		   }
		   return len;
	       }))
{
}

bool
StreamConnection::onReadable(ConnectionRegistry*)
{
    try {
	if (svcreg_->process(dec_.get(), enc_.get())) {
	    dec_->skipRecord();
	    enc_->pushRecord();
	    return true;
	}
    }
    catch (std::system_error&) {
    }
    return false;
}

ListenConnection::ListenConnection(
    int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg)
    : Connection(sock, bufferSize, svcreg)
{
}

bool
ListenConnection::onReadable(ConnectionRegistry* connreg)
{
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    auto newsock = ::accept(sock_, reinterpret_cast<sockaddr*>(&ss), &len);
    if (newsock < 0)
	throw std::system_error(errno, std::system_category());
    auto conn = std::make_shared<StreamConnection>(
	newsock, bufferSize_, svcreg_);
    connreg->add(conn);
    return true;
}

