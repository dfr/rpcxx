#include <random>

#include <unistd.h>
#include <sys/select.h>

#include <glog/logging.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/errors.h>
#include <rpc++/rpcbind.h>
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

template <typename Dur>
static auto toMilliseconds(Dur dur)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        dur).count();
}

std::chrono::seconds Channel::maxBackoff(30);

std::shared_ptr<Channel> Channel::open(const AddressInfo& ai)
{
    int s = socket(ai.family, ai.socktype, ai.protocol);
    if (s < 0) {
        throw std::system_error(errno, std::system_category());
    }
    if (ai.socktype == SOCK_STREAM) {
        auto chan = std::make_shared<StreamChannel>(s);
        chan->connect(ai.addr);
        return chan;
    }
    else {
        auto chan = std::make_shared<DatagramChannel>(s);
        chan->connect(ai.addr);
        return chan;
    }
}

std::shared_ptr<Channel> Channel::open(const std::vector<AddressInfo>& addrs)
{
    std::exception_ptr lastError;
    assert(addrs.size() > 0);
    for (auto& ai: addrs) {
        try {
            auto chan = Channel::open(ai);
            return chan;
        }
        catch (std::system_error& e) {
            lastError = std::current_exception();
        }
    }
    std::rethrow_exception(lastError);
}

std::shared_ptr<Channel> Channel::open(
    const std::string& host, uint32_t prog, uint32_t vers,
    const std::string& netid)
{
    auto rpcbind = RpcBind(Channel::open(host, "sunrpc", netid));
    auto uaddr = rpcbind.getaddr(rpcb{prog, vers, "", "", ""});
    if (uaddr == "") {
        throw RpcError("Program not registered");
    }
    return Channel::open(uaddr2taddr(uaddr, netid));
}

std::shared_ptr<Channel> Channel::open(
    const std::string& host, const std::string& service,
    const std::string& netid)
{
    return Channel::open(getAddressInfo(host, service, netid));
}

std::shared_ptr<Channel> Channel::open(
    const std::string& url, const std::string& netid)
{
    return Channel::open(getAddressInfo(url, netid));
}

Channel::Channel()
    : xid_(nextXid())
{
    retransmitInterval_ = std::chrono::seconds(1);
}

Channel::Channel(std::shared_ptr<ServiceRegistry> svcreg)
    : Channel()
{
    svcreg_ = svcreg;
}

Channel::~Channel()
{
    assert(pending_.size() == 0);
}

std::future<void>
Channel::callAsync(
    Client* client, uint32_t proc,
    std::function<void(XdrSink*)> xargs,
    std::function<void(XdrSource*)> xresults,
    Protection prot,
    clock_type::duration timeout)
{
    uint32_t xid;
    auto txp = new Transaction;
    auto& tx = *txp;

    auto now = clock_type::now();
    auto maxTime = now + timeout;
    int gen;
    std::unique_ptr<XdrMemory> xdrout;

    std::unique_lock<std::mutex> lock(mutex_);
    xid = tx.xid = xid_++;
    VLOG(3) << "assigning new xid: " << tx.xid;
    lock.unlock();

    for (;;) {
        gen = client->validateAuth(this, false);
        if (!gen) {
            using namespace std::placeholders;
            delete txp;
            return std::async(
                std::bind(&Channel::call, this, _1, _2, _3, _4, _5, _6),
                client, proc, xargs, xresults, prot, timeout);
        }

        xdrout = acquireBuffer();
        if (!client->processCall(
            xid, gen, proc, xdrout.get(), xargs, prot, tx.seq)) {
            continue;
        }
        break;
    }

    if (tman_) {
        // XXX: Should attempt to support retransmits here
        tx.tid = tman_->add(maxTime, [=]() {
            std::unique_lock<std::mutex> lock(mutex_);
            pending_.erase(xid);
            txp->continuation();
            delete txp;
        });
    }
    tx.async = true;
    tx.continuation = std::packaged_task<void()>([=]() {
        auto now = clock_type::now();
        auto& tx = *txp;
        if (!tx.body)
            throw TimeoutError();
        if (!processReply(client, proc, tx, prot, gen, xresults)) {
            // This should be rare - just process the call synchronously
            call(client, proc, xargs, xresults, prot, maxTime - now);
        }
    });
    tx.timeout = maxTime;

    lock.lock();
    pending_.emplace(xid, txp);
    lock.unlock();

    auto res = tx.continuation.get_future();
    sendMessage(std::move(xdrout));
    return res;
}

void
Channel::call(
    Client* client, uint32_t proc,
    std::function<void(XdrSink*)> xargs,
    std::function<void(XdrSource*)> xresults,
    Protection prot,
    clock_type::duration timeout)
{
    int nretries = 0;
    auto retransmitInterval = retransmitInterval_;

    uint32_t xid;
    Transaction tx;

    auto now = clock_type::now();
    auto maxTime = now + timeout;

    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (!tx.xid) {
            xid = tx.xid = xid_++;
            pending_.emplace(xid, &tx);
            VLOG(3) << "assigning new xid: " << tx.xid;
        }

        tx.state = Transaction::AUTH;
        VLOG(3) << "xid: " << xid << ": validating auth";
        lock.unlock();
        int gen = client->validateAuth(this);
        lock.lock();
        tx.state = Transaction::SEND;

        // Drop the lock while we transmit
        lock.unlock();
        auto xdrout = acquireBuffer();
        if (!client->processCall(
            xid, gen, proc, xdrout.get(), xargs, prot, tx.seq)) {
            lock.lock();
            continue;
        }
        sendMessage(std::move(xdrout));
        lock.lock();

        // XXX support oneway

        tx.timeout  = now + retransmitInterval;
        if (tx.timeout > maxTime)
            tx.timeout = maxTime;

        // Loop waiting for replies until we either get a matching
        // reply message or we time out
        for (;;) {
            tx.state = Transaction::REPLY;
            now = clock_type::now();
            assert(lock);
            if (!tx.body) {
                if (now >= tx.timeout) {
                    VLOG(3) << "xid: " << xid << " timeout";
                    break;
                }
                auto timeoutDuration = tx.timeout - now;
                if (running_) {
                    // Someone else is reading replies, wait until they wake
                    // us or until we time out.
                    VLOG(3) << "xid: " << xid << ": waiting for other thread: "
                            << toMilliseconds(timeoutDuration) << "ms";
                    tx.state = Transaction::SLEEPING;
                    tx.cv.wait_for(lock, timeoutDuration);
                    tx.state = Transaction::REPLY;
                }
                else {
                    running_ = true;
                    VLOG(3) << "xid: " << xid << ": waiting for reply: "
                            << toMilliseconds(timeoutDuration) << "ms";
                    processIncomingMessage(tx, lock, timeoutDuration);
                    running_ = false;
                }
            }

            // If we received a matching reply, we can stop waiting
            if (tx.body) {
                break;
            }
        }

        assert(lock);
        pending_.erase(xid);

        if (!tx.body) {
            // We timed out waiting for a reply - retransmit
            now = clock_type::now();
            if (now >= maxTime) {
                // XXX wakeup pending threads here?
                throw TimeoutError();
            }
            VLOG(3) << "xid: " << xid << ": retransmitting";
            nretries++;
            if (retransmitInterval < maxBackoff)
                retransmitInterval *= 2;
            continue;
        }

        VLOG(3) << "xid: " << xid << ": reply received";
        assert(tx.reply.xid == xid);
        tx.reply.xid = 0;
        tx.xid = 0;

        // If we have any pending transactions, make sure that at least
        // one thread is awake to read replies.
        if (pending_.size() > 0) {
            VLOG(3) << pending_.size() << " transactions pending";
            int liveThreads = 0;
            Transaction* toWake = nullptr;
            for (auto& i: pending_) {
                switch (i.second->state) {
                case Transaction::SEND:
                    break;
                case Transaction::AUTH:
                    break;
                case Transaction::REPLY:
                    liveThreads++;
                    break;
                case Transaction::SLEEPING:
                    toWake = i.second;
                    break;
                }
            }
            if (liveThreads == 0) {
                if (!toWake) {
                    // If there are no sleeping threads, then all the pending
                    // transactions must be in auth state. Exactly one of
                    // those threads must be performing the auth while the
                    // rest sleep. Since no non-auth transactions can be
                    // in-flight and we have just completed a transaction,
                    // it must be true that this thread is the auth performer
                    // which means that we also own one of the transactions
                    // in AUTH state - if we just return, that transaction
                    // will move back to AWAKE state and we can continue to
                    // make forward progress.
                }
                else {
                    VLOG(3) << "waking thread for " << "xid: " << toWake->xid;
                    toWake->cv.notify_one();
                }
            }
        }

        lock.unlock();
        if (processReply(client, proc, tx, prot, gen, xresults))
            break;
        lock.lock();
        tx.body.reset();
    }
}

bool Channel::processIncomingMessage(
    Transaction& tx,
    std::unique_lock<std::mutex>& lock,
    clock_type::duration timeout)
{
    auto now = clock_type::now();
    auto timeoutPoint = now + timeout;
    std::unique_ptr<XdrMemory> body;
    std::shared_ptr<Channel> replyChan;
    while (!body) {
        clock_type::time_point stopPoint;
        if (tman_) {
            stopPoint = std::min(timeoutPoint, tman_->next());
        }
        else {
            stopPoint = timeoutPoint;
        }
        assert(running_);
        VLOG(3) << "waiting for message ("
                << (tx.xid ? "client" : "server") << ")";
        lock.unlock();
        if (tman_) tman_->update(now);
        body = receiveMessage(replyChan, stopPoint - now);
        lock.lock();
        if (body)
            break;
        now = clock_type::now();
        if (tman_) tman_->update(now);
        if (now >= timeoutPoint)
            return false;
    }

    rpc_msg msg;
    try {
        xdr(msg, static_cast<XdrSource*>(body.get()));
    }
    catch (XdrError& e) {
        goto drop;
    }
    VLOG(3) << "xid: " << msg.xid <<": incoming message";
    if (msg.mtype == REPLY) {
        if (msg.xid == tx.xid) {
            VLOG(3) << "xid: " << msg.xid << ": matched reply";
            tx.reply = std::move(msg);
            tx.body = std::move(body);
            return true;
        }
        // This may be some other thread's reply. Find them and
        // wake them up to process it
        VLOG(3) << "xid: " << msg.xid << ": finding transaction";
        assert(lock);
        auto i = pending_.find(msg.xid);
        if (i != pending_.end()) {
            auto& other = *i->second;
            other.reply = std::move(msg);
            other.body = std::move(body);
            if (other.async) {
                // Note: the transaction was allocated in callAsync so we
                // need to free it here
                if (tman_) tman_->cancel(other.tid);
                pending_.erase(msg.xid);
                other.continuation();
                delete &other;
            }
            else {
                other.cv.notify_one();
            }
            return true;
        }
    }
    else if (msg.mtype == CALL && svcreg_) {
        lock.unlock();
        svcreg_->process(
            CallContext(std::move(msg), std::move(body), replyChan));
        return true;
    }

    // If we don't have a matching transaction, drop the message
    // If we don't find the transaction, just drop it. This can happen
    // for retransmits.
    VLOG(3) << "xid: " << msg.xid << ": dropping message";
drop:
    lock.unlock();
    releaseBuffer(std::move(body));
    lock.lock();
    return true;
}

bool
Channel::processReply(
    Client* client, uint32_t proc, Transaction& tx, Protection prot,
    int gen, std::function<void(XdrSource*)> xresults)
{
    if (tx.reply.mtype == REPLY
        && tx.reply.rbody().stat == MSG_ACCEPTED
        && tx.reply.rbody().areply().stat == SUCCESS) {
        if (!client->processReply(
            tx.seq, gen, tx.reply.rbody().areply(), tx.body.get(),
            xresults, prot)) {
            return false;
        }
        releaseBuffer(std::move(tx.body));
        return true;
    }
    else {
        releaseBuffer(std::move(tx.body));
        switch (tx.reply.rbody().stat) {
        case MSG_ACCEPTED: {
            const auto& areply = tx.reply.rbody().areply();
            switch (areply.stat) {
            case SUCCESS:
                // Handled above
                assert(false);

            case PROG_UNAVAIL:
                throw ProgramUnavailable(client->program());

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
            auto& rreply = tx.reply.rbody().rreply();
            switch (rreply.stat) {
            case RPC_MISMATCH:
                throw ProtocolMismatch(
                    rreply.rpc_mismatch.low,
                    rreply.rpc_mismatch.high);

            case AUTH_ERROR:
                // See if the client can refresh its creds
                if (client->authError(gen, rreply.auth_error)) {
                    return false;
                }
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

LocalChannel::LocalChannel(std::shared_ptr<ServiceRegistry> svcreg)
    : Channel(svcreg),
      bufferSize_(1500),       // XXX size
      replyChannel_(std::make_shared<ReplyChannel>(this))
{
}

std::unique_ptr<XdrMemory>
LocalChannel::acquireBuffer()
{
    return std::make_unique<XdrMemory>(bufferSize_);
}

void
LocalChannel::releaseBuffer(std::unique_ptr<XdrMemory>&& msg)
{
    msg.reset();
}

void
LocalChannel::sendMessage(std::unique_ptr<XdrMemory>&& msg)
{
    msg->setReadSize(msg->writePos());
    msg->rewind();
    rpc_msg call_msg;
    xdr(call_msg, static_cast<XdrSource*>(msg.get()));
    svcreg_->process(
        CallContext(
            std::move(call_msg), std::move(msg), replyChannel_));
}

std::unique_ptr<XdrMemory>
LocalChannel::ReplyChannel::acquireBuffer()
{
    return std::make_unique<XdrMemory>(chan_->bufferSize_);
}

void
LocalChannel::ReplyChannel::releaseBuffer(std::unique_ptr<XdrMemory>&& msg)
{
    msg.reset();
}

void
LocalChannel::ReplyChannel::sendMessage(std::unique_ptr<XdrMemory>&& msg)
{
    msg->setReadSize(msg->writePos());
    msg->rewind();
    std::unique_lock<std::mutex> lock(chan_->mutex_);
    chan_->queue_.emplace_back(std::move(msg));
}

std::unique_ptr<XdrMemory>
LocalChannel::ReplyChannel::receiveMessage(
    std::shared_ptr<Channel>&, clock_type::duration)
{
    return nullptr;
}

std::unique_ptr<XdrMemory>
LocalChannel::receiveMessage(
    std::shared_ptr<Channel>& replyChan, clock_type::duration timeout)
{
    replyChan = shared_from_this();
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() == 0)
        return nullptr;
    auto msg = std::move(queue_.front());
    queue_.pop_front();
    return std::move(msg);
}

void
LocalChannel::processReply()
{
    using namespace std::literals::chrono_literals;
    Transaction nulltx;
    std::unique_lock<std::mutex> lock(mutex_);
    assert(!running_);
    running_ = true;
    processIncomingMessage(nulltx, lock, 0s);
    running_ = false;
}

SocketChannel::SocketChannel(int sock)
    : Socket(sock)
{
}

SocketChannel::SocketChannel(int sock, std::shared_ptr<ServiceRegistry> svcreg)
    : Channel(svcreg),
      Socket(sock)
{
}

bool
SocketChannel::onReadable(SocketManager* sockman)
{
    using namespace std::literals::chrono_literals;
    Transaction nulltx;
    std::unique_lock<std::mutex> lock(mutex_);
    if (running_)
        // Some other thread is reading from the socket
        return true;
    running_ = true;
    try {
        processIncomingMessage(nulltx, lock, 0s);
        running_ = false;
        return true;
    }
    catch (std::system_error& e) {
        running_ = false;
        return false;
    }
    catch (XdrError& e) {
        running_ = false;
        return false;
    }
}

DatagramChannel::DatagramChannel(int sock)
    : SocketChannel(sock),
      bufferSize_(1500),
      xdrs_(std::make_unique<XdrMemory>(bufferSize_))
{
}

DatagramChannel::DatagramChannel(
    int sock, std::shared_ptr<ServiceRegistry> svcreg)
    : DatagramChannel(sock)
{
    svcreg_ = svcreg;
}

void
DatagramChannel::connect(const Address& addr)
{
    remoteAddr_ = addr;
}

std::unique_ptr<XdrMemory>
DatagramChannel::acquireBuffer()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (xdrs_) {
        xdrs_->rewind();
        xdrs_->setWriteSize(xdrs_->bufferSize());
        return std::move(xdrs_);
    }
    else {
        return std::make_unique<XdrMemory>(bufferSize_);
    }
}

void
DatagramChannel::releaseBuffer(std::unique_ptr<XdrMemory>&& msg)
{
    std::unique_lock<std::mutex> lock(mutex_);
    xdrs_ = std::move(msg);
}

void
DatagramChannel::sendMessage(std::unique_ptr<XdrMemory>&& msg)
{
    sendto(msg->buf(), msg->writePos(), remoteAddr_);
    releaseBuffer(std::move(msg));
}

std::unique_ptr<XdrMemory>
DatagramChannel::receiveMessage(
    std::shared_ptr<Channel>& replyChan, clock_type::duration timeout)
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

    Address addr;
    auto bytes = recvfrom(msg->buf(), msg->bufferSize(), addr);
    if (bytes == 0)
        return nullptr;

    // We could try to cache reply channels here but there is little point
    // since the allocation is cheap and datagram sockets are discouraged
    // for high-performance applications
    replyChan = std::make_shared<DatagramReplyChannel>(fd_, addr);    msg->rewind();
    msg->setReadSize(bytes);
    return std::move(msg);
}

StreamChannel::StreamChannel(int sock)
    : SocketChannel(sock),
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

StreamChannel::StreamChannel(
    int sock, std::shared_ptr<ServiceRegistry> svcreg)
    : StreamChannel(sock)
{
    svcreg_ = svcreg;
}

StreamChannel::~StreamChannel()
{
}

std::unique_ptr<XdrMemory>
StreamChannel::acquireBuffer()
{
    std::unique_lock<std::mutex> lock(writeMutex_);
    std::unique_ptr<XdrMemory> msg = std::move(sendbuf_);
    if (!msg) {
        msg = std::make_unique<XdrMemory>(bufferSize_);
    }
    return std::move(msg);
}

void
StreamChannel::releaseBuffer(std::unique_ptr<XdrMemory>&& msg)
{
    std::unique_lock<std::mutex> lock(writeMutex_);
    // XXX: rework the receive path to use standard sized buffers
    if (msg->bufferSize() != bufferSize_) {
        msg.reset();
        return;
    }
    msg->rewind();
    sendbuf_ = std::move(msg);
}

void
StreamChannel::sendMessage(std::unique_ptr<XdrMemory>&& msg)
{
    std::unique_lock<std::mutex> lock(writeMutex_);
    sender_->putBytes(msg->buf(), msg->writePos());
    sender_->pushRecord();
    msg->rewind();
    sendbuf_ = std::move(msg);
}

void
StreamChannel::readAll(void* buf, size_t len)
{
    auto p = reinterpret_cast<uint8_t*>(buf);
    auto n = len;

    while (n > 0) {
        auto bytes = recv(p, n);
        if (bytes == 0)
            throw std::system_error(ENOTCONN, std::system_category());
        p += bytes;
        n -= bytes;
    }
}

std::unique_ptr<XdrMemory>
StreamChannel::receiveMessage(
    std::shared_ptr<Channel>& replyChan, clock_type::duration timeout)
{
    if (!waitForReadable(timeout))
        return nullptr;

    replyChan = shared_from_this();
    bool done = false;
    std::deque<std::unique_ptr<XdrMemory>> fragments;
    size_t total = 0;
    while (!done) {
        uint8_t recbuf[sizeof(uint32_t)];
        readAll(recbuf, sizeof(uint32_t));
        uint32_t rec = *reinterpret_cast<const XdrWord*>(recbuf);
        uint32_t reclen = rec & 0x7fffffff;
        done = (rec & (1 << 31)) != 0;
        VLOG(4) << reclen << " byte record, eor=" << done;
        auto frag = std::make_unique<XdrMemory>(reclen);
        readAll(frag->buf(), reclen);
        VLOG(4) << "read fragment body";
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

ptrdiff_t
StreamChannel::write(const void* buf, size_t len)
{
    // This will always be called via sendMessage with writeMutex_ held.
    auto p = reinterpret_cast<const uint8_t*>(buf);
    auto n = len;

    VLOG(3) << "writing " << len << " bytes to socket";
    while (n > 0) {
        auto bytes = send(p, len);
        if (bytes == 0)
            throw std::system_error(ENOTCONN, std::system_category());
        p += bytes;
        n -= bytes;
    }
    return len;
}

bool
ListenSocket::onReadable(SocketManager* sockman)
{
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    auto newsock = ::accept(fd_, reinterpret_cast<sockaddr*>(&ss), &len);
    if (newsock < 0)
        throw std::system_error(errno, std::system_category());
    VLOG(3) << "New connection fd: " << newsock;
    sockman->add(std::make_shared<StreamChannel>(newsock, svcreg_));
    return true;
}
