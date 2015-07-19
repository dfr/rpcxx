// -*- c++ -*-

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

#include <rpc++/client.h>
#include <rpc++/rec.h>
#include <rpc++/rpcproto.h>
#include <rpc++/socket.h>
#include <rpc++/util.h>
#include <rpc++/xdr.h>

namespace oncrpc {

class XdrSink;
class XdrSource;
class RecordReader;
class RecordWriter;
class ServiceRegistry;

class Channel: public std::enable_shared_from_this<Channel>
{
protected:
    struct Transaction;

public:
    static std::chrono::seconds maxBackoff;

    /// Create an RPC channel
    Channel();
    Channel(std::shared_ptr<ServiceRegistry> svcreg);

    virtual ~Channel();

    /// Make a remote procedure call
    virtual void call(
        Client* client, uint32_t proc,
        std::function<void(XdrSink*)> xargs,
        std::function<void(XdrSource*)> xresults,
        Protection prot = Protection::DEFAULT,
        std::chrono::system_clock::duration timeout = std::chrono::seconds(30));

    /// Read a message from the channel. If the message is a reply, try to
    /// match it with a pending call transaction and hand off a suitable
    /// XdrSource to that transaction. Return true if a message was received
    /// otherwise false if the timeout was reached.
    bool processIncomingMessage(
        Transaction& tx, std::unique_lock<std::mutex>& lock,
        std::chrono::system_clock::duration timeout);

    /// Return a buffer suitable for encoding an outgoing message. When
    /// the message is complete, call sendMessage to send it to the remote
    /// endpoint.
    virtual std::unique_ptr<XdrMemory> acquireBuffer() = 0;

    /// Discard a message buffer returned by acquireBuffer or receiveMessage.
    virtual void releaseBuffer(std::unique_ptr<XdrMemory>&& msg) = 0;

    /// Send the message to the remote endpoint. The message pointer should
    /// be one previously returned by acquireBuffer and will be released
    /// as for releaseBuffer.
    virtual void sendMessage(std::unique_ptr<XdrMemory>&& msg) = 0;

    /// Receive an incoming message from the channel. If a message was
    /// received, a buffer containing the message is returned.
    /// This pointer should be released after processing using releaseBuffer.
    /// If no message was received before the timeout, a null pointer is
    /// returned.
    virtual std::unique_ptr<XdrMemory> receiveMessage(
        std::chrono::system_clock::duration timeout) = 0;

protected:
    struct Transaction {
        Transaction()
            : cv(new std::condition_variable)
        {
        }

        Transaction(Transaction&& other)
            : cv(std::move(other.cv)),
              reply(std::move(other.reply))
        {
        }

        uint32_t xid = 0;
        uint32_t seq = 0;
        bool sleeping = false;
        std::unique_ptr<std::condition_variable> cv; // signalled when ready
        rpc_msg reply;
        std::unique_ptr<XdrMemory> body;
    };

    uint32_t xid_;
    std::chrono::system_clock::duration retransmitInterval_;

    // The mutex serialises access to running_, pending_ and all
    // transactions contained in pending_
    std::mutex mutex_;
    bool running_ = false;      // true if a thread is reading
    std::unordered_map<uint32_t, Transaction> pending_; // in-flight calls
    std::shared_ptr<ServiceRegistry> svcreg_;
};

/// Process RPC calls using the given registry of local
/// services. Thread safe.
class LocalChannel: public Channel
{
public:
    LocalChannel(std::shared_ptr<ServiceRegistry> svcreg);

    // Channel overrides
    std::unique_ptr<XdrMemory> acquireBuffer() override;
    void releaseBuffer(std::unique_ptr<XdrMemory>&& msg) override;
    void sendMessage(std::unique_ptr<XdrMemory>&& msg) override;
    std::unique_ptr<XdrMemory> receiveMessage(
        std::chrono::system_clock::duration timeout) override;

private:
    friend struct ReplyChannel;
    struct ReplyChannel: public Channel
    {
        ReplyChannel(LocalChannel* chan)
            : chan_(chan)
        {
        }

        // Channel overrides
        std::unique_ptr<XdrMemory> acquireBuffer() override;
        void releaseBuffer(std::unique_ptr<XdrMemory>&& msg) override;
        void sendMessage(std::unique_ptr<XdrMemory>&& msg) override;
        std::unique_ptr<XdrMemory> receiveMessage(
            std::chrono::system_clock::duration timeout) override;

        LocalChannel* chan_;
    };

    size_t bufferSize_;
    std::deque<std::unique_ptr<XdrMemory>> queue_;
    std::shared_ptr<ReplyChannel> replyChannel_;
};

/// Send or receive RPC messages over a socket. Thread safe.
class SocketChannel: public Channel, public Socket
{
public:
    SocketChannel(int sock);
    SocketChannel(int sock, std::shared_ptr<ServiceRegistry>);

    // Socket overrides
    bool onReadable(SocketManager* sockman) override;
};

/// Send or receive RPC messages over a socket. Thread safe.
class DatagramChannel: public SocketChannel
{
public:
    DatagramChannel(int sock);
    DatagramChannel(int sock, std::shared_ptr<ServiceRegistry>);

    // Channel overrides
    std::unique_ptr<XdrMemory> acquireBuffer() override;
    void releaseBuffer(std::unique_ptr<XdrMemory>&& msg) override;
    void sendMessage(std::unique_ptr<XdrMemory>&& msg) override;
    std::unique_ptr<XdrMemory> receiveMessage(
        std::chrono::system_clock::duration timeout) override;

private:
    size_t bufferSize_;
    std::unique_ptr<XdrMemory> xdrs_;
};

/// Send RPC messages over a connected stream socket. Thread safe.
class StreamChannel: public SocketChannel
{
public:
    StreamChannel(int sock);
    StreamChannel(int sock, std::shared_ptr<ServiceRegistry>);

    ~StreamChannel();

    // Channel overrides
    std::unique_ptr<XdrMemory> acquireBuffer() override;
    void releaseBuffer(std::unique_ptr<XdrMemory>&& msg) override;
    void sendMessage(std::unique_ptr<XdrMemory>&& msg) override;
    std::unique_ptr<XdrMemory> receiveMessage(
        std::chrono::system_clock::duration timeout) override;

private:
    ptrdiff_t write(const void* buf, size_t len);

    size_t bufferSize_;

    // Protects sendbuf_ and sender_
    std::mutex writeMutex_;
    std::unique_ptr<XdrMemory> sendbuf_;
    std::unique_ptr<RecordWriter> sender_;
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

    // Socket overrides
    bool onReadable(SocketManager* sockman) override;

private:
    std::shared_ptr<ServiceRegistry> svcreg_;
};

}
