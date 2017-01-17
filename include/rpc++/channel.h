/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

// -*- c++ -*-

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <unordered_map>

#include <rpc++/client.h>
#include <rpc++/rec.h>
#include <rpc++/rpcproto.h>
#include <rpc++/socket.h>
#include <rpc++/timeout.h>
#include <rpc++/xdr.h>

namespace oncrpc {

class XdrSink;
class XdrSource;
class RecordReader;
class RecordWriter;
class RestChannel;
class RestRegistry;
class ServiceRegistry;

class Message: public XdrMemory
{
public:
    Message(size_t sz);

    size_t writePos() const
    {
        return XdrMemory::writePos() + refBytes_;
    }

    /// Reset the message back to empty
    void rewind()
    {
        refBytes_ = 0;
        readIndex_ = 0;
        readCursor_ = readLimit_ = nullptr;
        writeCursor_ = buf_;
        writeLimit_ = buf_ + size_;
        iov_.clear();
        iov_.emplace_back(iovec{writeCursor_, 0});
        buffers_.clear();
    }

    /// Advance the write cursor. Typically used after reading into the buffer
    /// from some other source
    void advanceWrite(size_t sz)
    {
        assert(writeCursor_ + sz <= writeLimit_);
        writeCursor_ += sz;
    }

    auto iov() const { return iov_; }

    void copyTo(XdrSink* xdrs)
    {
        int j = 0;
        auto iovp = &iov_[0];
        for (int i = 0; i < int(iov_.size()); i++) {
            if (j < buffers_.size() && buffers_[j]->data() == iovp->iov_base) {
                xdrs->putBuffer(buffers_[j]);
                j++;
            }
            else if (iovp->iov_base == pad_) {
                // Skip - putBuffer above handles this
            }
            else {
                xdrs->putBytes(iovp->iov_base, iovp->iov_len);
            }
	    iovp++;
        }
    }

    // XdrSink overrides
    void putBuffer(const std::shared_ptr<Buffer>& buf) override;
    void flush() override;

    // XdrSource overrides
    size_t readSize() const override;
    void fill() override;

private:
    uint8_t pad_[4] = {0,0,0,0};
    std::vector<iovec> iov_;
    std::vector<std::shared_ptr<Buffer>> buffers_;
    size_t refBytes_ = 0;
    int readIndex_ = 0;
};

class Channel: public std::enable_shared_from_this<Channel>
{
protected:
    struct Transaction;

public:
    typedef std::chrono::system_clock clock_type;
    static std::chrono::seconds maxBackoff;

    static constexpr int DEFAULT_BUFFER_SIZE = 1500;

    /// Helper function for creating and opening channels
    static std::shared_ptr<Channel> open(const AddressInfo& ai);
    static std::shared_ptr<Channel> open(
        const std::vector<AddressInfo>& addrs,
        bool connectAll = false);
    static std::shared_ptr<Channel> open(
        const std::string& host, uint32_t prog, uint32_t vers,
        const std::string& netid);
    static std::shared_ptr<Channel> open(
        const std::string& host, const std::string& service,
        const std::string& netid);
    static std::shared_ptr<Channel> open(
        const std::string& url, const std::string& netid = "",
        bool connectAll = false);

    /// Create an RPC channel
    Channel();
    Channel(std::shared_ptr<ServiceRegistry> svcreg);

    virtual ~Channel();

    /// Set a timeout manager for this channel. This is required to support
    /// timeouts for async calls. Note: caller is responsible for the
    /// TimeoutManager lifetime
    void setTimeoutManager(TimeoutManager* tman)
    {
        tman_ = tman;
    }

    /// Make an asynchronous remote procedure call
    std::future<void> callAsync(
        Client* client, uint32_t proc,
        std::function<void(XdrSink*)> xargs,
        std::function<void(XdrSource*)> xresults,
        Protection prot = Protection::DEFAULT,
        clock_type::duration timeout = std::chrono::seconds(30));

    /// Make a remote procedure call
    void call(
        Client* client, uint32_t proc,
        std::function<void(XdrSink*)> xargs,
        std::function<void(XdrSource*)> xresults,
        Protection prot = Protection::DEFAULT,
        clock_type::duration timeout = std::chrono::seconds(30));

    /// Send a remote procedure call without waiting for a reply. Any
    /// replies which are received will be dropped.
    void send(
        Client* client, uint32_t proc,
        std::function<void(XdrSink*)> xargs,
        Protection prot = Protection::DEFAULT);

    /// Return a buffer suitable for encoding an outgoing message. When
    /// the message is complete, call sendMessage to send it to the remote
    /// endpoint.
    virtual std::unique_ptr<XdrSink> acquireSendBuffer() = 0;

    /// Discard a message buffer returned by acquireBuffer or receiveMessage.
    virtual void releaseSendBuffer(std::unique_ptr<XdrSink>&& msg) = 0;

    /// Send the message to the remote endpoint. The message pointer should
    /// be one previously returned by acquireBuffer and will be released
    /// as for releaseBuffer.
    virtual void sendMessage(std::unique_ptr<XdrSink>&& msg) = 0;

    /// Receive an incoming message from the channel. If a message was
    /// received, a buffer containing the message is returned.
    /// This pointer should be released after processing using releaseBuffer.
    /// If no message was received before the timeout, a null pointer is
    /// returned.
    virtual std::unique_ptr<XdrSource> receiveMessage(
        std::shared_ptr<Channel>& replyChan, clock_type::duration timeout) = 0;

    /// Discard a message buffer returned by acquireBuffer or receiveMessage.
    virtual void releaseReceiveBuffer(std::unique_ptr<XdrSource>&& msg) = 0;

    /// Return the current channel buffer size
    auto bufferSize() const { return bufferSize_; }

    /// Set the channel buffer size
    void setBufferSize(size_t sz) { bufferSize_ = sz; }

    /// Return the channel's service registry, if any
    std::shared_ptr<ServiceRegistry> serviceRegistry() const
    {
        return svcreg_.lock();
    }

    /// Set the service registry to use for handlng calls on this
    /// channel
    void setServiceRegistry(std::shared_ptr<ServiceRegistry> svcreg)
    {
	svcreg_ = svcreg;
    }

    /// For server-side channels, set a flag to control whether to
    /// close the channel when idle.
    virtual void setCloseOnIdle(bool closeOnIdle) {}

    /// Register a callback which is called if the channel is
    /// re-connected to the remote endpoint.
    virtual void onReconnect(std::function<void()> cb) {}

    /// Return the network address of the other end of this channel
    virtual AddressInfo remoteAddress() const { return AddressInfo{}; }

protected:

    /// Read a message from the channel. If the message is a reply, try to
    /// match it with a pending call transaction and hand off a suitable
    /// XdrSource to that transaction. Return true if a message was received
    /// otherwise false if the timeout was reached.
    bool processIncomingMessage(
        Transaction& tx, std::unique_lock<std::mutex>& lock,
        clock_type::duration timeout);

    /// Parse a reply message, possibly decoding the reply body. Returns
    /// true if the call is complete, otherwise false if the message should
    /// be re-sent.
    bool processReply(
        Client* client, uint32_t proc, Transaction& tx, Protection prot,
        int gen, std::function<void(XdrSource*)> xresults);

    struct Transaction {
        enum {
            SEND,       // sending message
            AUTH,       // possibly asleep performing authentication
            REPLY,      // waiting for some reply message
            SLEEPING,   // sleeping in Channel::call waiting for reply
            RESEND      // message must be re-sent after a socket reconnect
        } state = SEND;
        uint32_t xid = 0;
        uint32_t seq = 0;
        bool sleeping = false;
        std::condition_variable cv; // signalled when ready
        clock_type::time_point timeout;
        rpc_msg reply;
        std::unique_ptr<XdrSource> body;
        TimeoutManager::task_type tid = 0;
        bool async = false;
        std::packaged_task<void()> continuation;
    };

    uint32_t xid_;
    size_t bufferSize_ = DEFAULT_BUFFER_SIZE;
    clock_type::duration retransmitInterval_;

    // The mutex serialises access to running_, pending_ and all
    // transactions contained in pending_
    std::mutex mutex_;
    bool running_ = false;      // true if a thread is reading
    std::unordered_map<uint32_t, Transaction*> pending_; // in-flight calls
    std::weak_ptr<ServiceRegistry> svcreg_;
    TimeoutManager* tman_ = nullptr;  // XXX: observer_ptr
};

/// Process RPC calls using the given registry of local
/// services. Thread safe.
class LocalChannel: public Channel
{
public:
    LocalChannel(std::shared_ptr<ServiceRegistry> svcreg);

    // Channel overrides
    std::unique_ptr<XdrSink> acquireSendBuffer() override;
    void releaseSendBuffer(std::unique_ptr<XdrSink>&& msg) override;
    void sendMessage(std::unique_ptr<XdrSink>&& msg) override;
    std::unique_ptr<XdrSource> receiveMessage(
        std::shared_ptr<Channel>& replyChan,
        clock_type::duration timeout) override;
    void releaseReceiveBuffer(std::unique_ptr<XdrSource>&& msg) override;

    /// Process a single queued reply - intended for testing
    void processReply();

private:
    std::deque<std::unique_ptr<XdrMemory>> queue_;
};

/// Send or receive RPC messages over a socket. Thread safe.
class SocketChannel: public Channel, public Socket
{
public:
    SocketChannel(int sock);
    SocketChannel(int sock, std::shared_ptr<ServiceRegistry>);

    // Channel overrides
    void setCloseOnIdle(bool closeOnIdle) override
    {
	Socket::setCloseOnIdle(closeOnIdle);
    }

    // Socket overrides
    bool onReadable(SocketManager* sockman) override;
};

/// Send or receive RPC messages over a socket. Thread safe.
class DatagramChannel: public SocketChannel
{
public:
    DatagramChannel(int sock);
    DatagramChannel(int sock, std::shared_ptr<ServiceRegistry>);

    // Socket overrides - we allow the channel to be 'connected' to
    // multiple addresses to emulate multicast in environments which
    // don't support it
    void connect(const Address& addr) override;

    // Channel overrides
    std::unique_ptr<XdrSink> acquireSendBuffer() override;
    void releaseSendBuffer(std::unique_ptr<XdrSink>&& msg) override;
    void sendMessage(std::unique_ptr<XdrSink>&& msg) override;
    std::unique_ptr<XdrSource> receiveMessage(
        std::shared_ptr<Channel>& replyChan,
        clock_type::duration timeout) override;
    void releaseReceiveBuffer(std::unique_ptr<XdrSource>&& msg) override;
    AddressInfo remoteAddress() const override;

protected:
    std::vector<Address> remoteAddrs_;
    std::unique_ptr<Message> xdrs_;
};

struct DatagramReplyChannel: public DatagramChannel
{
public:
    DatagramReplyChannel(int fd_, const Address& addr)
        : DatagramChannel(fd_)
    {
        remoteAddrs_.push_back(addr);
    }

    ~DatagramReplyChannel()
    {
        // Don't close the socket
        setFd(-1);
    }
};

/// Send RPC messages over a connected stream socket. Thread safe.
class StreamChannel: public SocketChannel
{
public:
    StreamChannel(int sock);
    StreamChannel(int sock, std::shared_ptr<ServiceRegistry> svcreg);
    StreamChannel(
        int sock,
        std::shared_ptr<ServiceRegistry> svcreg,
        std::shared_ptr<RestRegistry> restreg);

    ~StreamChannel();

    // Socket overrides
    bool onReadable(SocketManager* sockman) override;

    // Channel overrides
    std::unique_ptr<XdrSink> acquireSendBuffer() override;
    void releaseSendBuffer(std::unique_ptr<XdrSink>&& msg) override;
    void sendMessage(std::unique_ptr<XdrSink>&& msg) override;
    std::unique_ptr<XdrSource> receiveMessage(
        std::shared_ptr<Channel>& replyChan,
        clock_type::duration timeout) override;
    void releaseReceiveBuffer(std::unique_ptr<XdrSource>&& msg) override;
    AddressInfo remoteAddress() const override;

private:
    void readAll(void* buf, size_t len);

    // Optional REST api support
    std::weak_ptr<RestRegistry> restreg_;
    std::shared_ptr<RestChannel> restchan_;

    // Protects sendbuf_ and sender_
    std::mutex writeMutex_;
    std::unique_ptr<Message> sendbuf_;
};

/// A specialisation of StreamChannel which re-connects the channel if The
/// remote endpoint is closed
class ReconnectChannel: public StreamChannel
{
public:
    ReconnectChannel(int sock, const AddressInfo& ai);

    /// Reconnect the socket
    void reconnect();

    // Channel overrides
    void onReconnect(std::function<void()> cb) override
    {
        reconnectCallback_ = cb;
    }

    // Socket overrides
    ssize_t send(const std::vector<iovec>& iov) override;
    ssize_t recv(void* buf, size_t buflen) override;

private:
    AddressInfo addrinfo_;
    std::function<void()> reconnectCallback_;
};

/// Accept incoming connections to a socket and create instances of
/// StreamChannel for each new connection.
class ListenSocket: public Socket
{
public:
    ListenSocket(int fd, std::shared_ptr<ServiceRegistry> svcreg)
        : Socket(fd),
          svcreg_(svcreg)
    {
    }

    ListenSocket(
        int fd,
        std::shared_ptr<ServiceRegistry> svcreg,
        std::shared_ptr<RestRegistry> restreg)
        : Socket(fd),
          svcreg_(svcreg),
          restreg_(restreg)
    {
    }

    /// Return the buffer size for new channels
    auto bufferSize() const { return bufferSize_; }

    /// Set the channel buffer size for new channels
    void setBufferSize(size_t sz) { bufferSize_ = sz; }

    // Socket overrides
    bool onReadable(SocketManager* sockman) override;

private:
    std::weak_ptr<ServiceRegistry> svcreg_;
    std::weak_ptr<RestRegistry> restreg_;
    size_t bufferSize_ = Channel::DEFAULT_BUFFER_SIZE;
};

}
