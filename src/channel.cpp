#include <random>

#include <unistd.h>
#include <sys/select.h>

#include <glog/logging.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/errors.h>
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

std::chrono::seconds Channel::maxBackoff(30);

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

void
Channel::call(
    Client* client, uint32_t proc,
    std::function<void(XdrSink*)> xargs,
    std::function<void(XdrSource*)> xresults,
    Protection prot,
    std::chrono::system_clock::duration timeout)
{
    int nretries = 0;
    auto retransmitInterval = retransmitInterval_;

call_again:
    int gen = client->validateAuth(this);

    std::unique_lock<std::mutex> lock(mutex_);

    auto xid = xid_++;
    VLOG(3) << "xid: " << xid << ": new call";
    auto i = pending_.emplace(xid, std::move(Transaction()));
    auto& tx = i.first->second;
    tx.xid = xid;

    auto now = std::chrono::system_clock::now();
    auto maxTime = now + timeout;

    for (;;) {
        // Drop the lock while we transmit
        lock.unlock();
        auto xdrout = acquireBuffer();
        if (!client->processCall(
            xid, tx.seq, proc, xdrout.get(), xargs, prot))
            goto call_again;
        sendMessage(std::move(xdrout));
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
                    auto timeoutDuration = retransmitTime - now;
                    VLOG(3) << "xid: " << xid << ": waiting for other thread: "
                            << std::chrono::duration_cast<std::chrono::milliseconds>(timeoutDuration).count()
                            << "ms";
                    tx.sleeping = true;
                    auto status = tx.cv->wait_for(lock, timeoutDuration);
                    tx.sleeping = false;
                    if (status == std::cv_status::no_timeout) {
                        if (!tx.body) {
                            // Some other thread woke us up so that we can
                            // take over reading replies
                            continue;
                        }
                    }
                    else {
                        VLOG(3) << "xid: " << xid
                                << ": timed out waiting for other thread";
                        break;
                    }
                }
                else {
                    running_ = true;
                    VLOG(3) << "xid: " << xid << ": waiting for reply";
                    auto received = processIncomingMessage(
                        tx, lock, retransmitTime - now);
                    running_ = false;
                    if (!received)
                        break;
                }
            }

            // If we received a matching reply, we can stop waiting
            if (tx.body) {
                break;
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
            VLOG(3) << "xid: " << xid << ": retransmitting";
            nretries++;
            if (retransmitInterval < maxBackoff)
                retransmitInterval *= 2;
            continue;
        }

        assert(tx.reply.xid == xid);
        VLOG(3) << "xid: " << xid << ": reply received";

        auto reply_msg = std::move(tx.reply);
        auto xdrin = std::move(tx.body);
        auto seq = tx.seq;
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
                VLOG(3) << "waking thread for " << "xid: " << i->first;
                i->second.cv->notify_one();
            }
        }

        lock.unlock();

        if (reply_msg.mtype == REPLY
            && reply_msg.rbody().stat == MSG_ACCEPTED
            && reply_msg.rbody().areply().stat == SUCCESS) {
            if (!client->processReply(
                seq, gen, reply_msg.rbody().areply(), xdrin.get(),
                xresults, prot))
                goto call_again;
            releaseBuffer(std::move(xdrin));
            break;
        }
        else {
            releaseBuffer(std::move(xdrin));
            switch (reply_msg.rbody().stat) {
            case MSG_ACCEPTED: {
                const auto& areply = reply_msg.rbody().areply();
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
                auto& rreply = reply_msg.rbody().rreply();
                switch (rreply.stat) {
                case RPC_MISMATCH:
                    throw ProtocolMismatch(
                        rreply.rpc_mismatch.low,
                        rreply.rpc_mismatch.high);

                case AUTH_ERROR:
                    if (client->authError(gen, rreply.auth_error))
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

bool Channel::processIncomingMessage(
    Transaction& tx,
    std::unique_lock<std::mutex>& lock,
    std::chrono::system_clock::duration timeout)
{
    assert(running_);
    lock.unlock();
    auto body = receiveMessage(timeout);
    lock.lock();
    if (!body)
        return false;

    rpc_msg msg;
    try {
        xdr(msg, static_cast<XdrSource*>(body.get()));
    }
    catch (XdrError& e) {
        goto drop;
    }
    if (msg.mtype == REPLY) {
        if (msg.xid == tx.xid) {
            tx.reply = std::move(msg);
            tx.body = std::move(body);
            return true;
        }
        // This may be some other thread's reply. Find them and
        // wake them up to process it
        VLOG(3) << "xid: " << msg.xid << ": finding transaction";
        auto i = pending_.find(msg.xid);
        if (i != pending_.end()) {
            auto& other = i->second;
            other.reply = std::move(msg);
            other.body = std::move(body);
            other.cv->notify_one();
            return true;
        }
    }
    else if (msg.mtype == CALL && svcreg_) {
        lock.unlock();
        svcreg_->process(
            CallContext(std::move(msg), std::move(body), shared_from_this()));
        return true;
    }

    // If we don't have a matching transaction, drop the message
    // If we don't find the transaction, just drop
    // it. This can happen for retransmits.
    VLOG(3) << "xid: " << msg.xid << ": dropping message";
drop:
    lock.unlock();
    releaseBuffer(std::move(body));
    lock.lock();
    return true;
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
    std::chrono::system_clock::duration timeout)
{
    return nullptr;
}

std::unique_ptr<XdrMemory>
LocalChannel::receiveMessage(std::chrono::system_clock::duration timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() == 0)
        return nullptr;
    auto msg = std::move(queue_.front());
    queue_.pop_front();
    return std::move(msg);
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
    ::write(fd_, msg->buf(), msg->writePos());
    releaseBuffer(std::move(msg));
}

std::unique_ptr<XdrMemory>
DatagramChannel::receiveMessage(std::chrono::system_clock::duration timeout)
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

    auto bytes = ::read(fd_, msg->buf(), msg->bufferSize());
    if (bytes <= 0) {
        throw std::system_error(errno, std::system_category());
    }
    msg->rewind();
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

static void
readAll(int sock, void* buf, size_t len)
{
    auto p = reinterpret_cast<uint8_t*>(buf);
    auto n = len;

    while (n > 0) {
        auto bytes = ::read(sock, p, n);
        if (bytes <= 0)
            throw std::system_error(errno, std::system_category());
        p += bytes;
        n -= bytes;
    }
}

std::unique_ptr<XdrMemory>
StreamChannel::receiveMessage(std::chrono::system_clock::duration timeout)
{
    VLOG(3) << "waiting for reply";
    if (!waitForReadable(timeout))
        return nullptr;
    VLOG(3) << "socket is readable";

    bool done = false;
    std::deque<std::unique_ptr<XdrMemory>> fragments;
    size_t total = 0;
    while (!done) {
        uint8_t recbuf[sizeof(uint32_t)];
        readAll(fd_, recbuf, sizeof(uint32_t));
        uint32_t rec = *reinterpret_cast<const XdrWord*>(recbuf);
        uint32_t reclen = rec & 0x7fffffff;
        done = (rec & (1 << 31)) != 0;
        VLOG(4) << reclen << " byte record, eor=" << done;
        auto frag = std::make_unique<XdrMemory>(reclen);
        readAll(fd_, frag->buf(), reclen);
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
        auto bytes = ::write(fd_, p, len);
        if (bytes < 0)
            throw std::system_error(errno, std::system_category());
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
