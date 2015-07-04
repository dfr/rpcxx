// -*- c++ -*-

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>

#include <rpc++/rec.h>
#include <rpc++/rpcproto.h>
#include <rpc++/socket.h>
#include <rpc++/util.h>
#include <rpc++/xdr.h>

namespace oncrpc {

class Client;
class XdrSink;
class XdrSource;
class RecordReader;
class RecordWriter;
class ServiceRegistry;

/// MSG_ACCEPTED, PROG_UNAVAIL
class ProgramUnavailable: public RpcError
{
public:
    ProgramUnavailable(uint32_t prog);

    uint32_t prog() const { return prog_; }

private:
    uint32_t prog_;
};

/// MSG_ACCEPTED, PROC_UNAVAIL
class ProcedureUnavailable: public RpcError
{
public:
    ProcedureUnavailable(uint32_t prog);

    uint32_t proc() const { return proc_; }

private:
    uint32_t proc_;
};

/// MSG_ACCEPTED, PROG_MISMATCH
class VersionMismatch: public RpcError
{
public:
    VersionMismatch(uint32_t minver, uint32_t maxver);

    uint32_t minver() const { return minver_; }
    uint32_t maxver() const { return maxver_; }

private:
    uint32_t minver_;
    uint32_t maxver_;
};

/// MSG_ACCEPTED, GARBAGE_ARGS
class GarbageArgs: public RpcError
{
public:
    GarbageArgs();
};

/// MSG_ACCEPTED, SYSTEM_ERR
class SystemError: public RpcError
{
public:
    SystemError();
};

/// MSG_DENIED, RPC_MISMATCH
class ProtocolMismatch: public RpcError
{
public:
    ProtocolMismatch(uint32_t minver, uint32_t maxver);

    uint32_t minver() const { return minver_; }
    uint32_t maxver() const { return maxver_; }

private:
    uint32_t minver_;
    uint32_t maxver_;
};

/// MSG_DENIED, AUTH_ERROR
class AuthError: public RpcError
{
public:
    AuthError(auth_stat stat);

    auth_stat stat() const { return stat_; }

private:
    auth_stat stat_;
};

class Channel: public std::enable_shared_from_this<Channel>
{
protected:
    struct Transaction;

public:
    static std::chrono::seconds maxBackoff;

    /// Create an RPC channel
    Channel();
    Channel(std::shared_ptr<ServiceRegistry> svcreg);

    ~Channel();

    /// Make a remote procedure call
    virtual void call(
        Client* client, uint32_t proc,
        std::function<void(XdrSink*)> xargs,
        std::function<void(XdrSource*)> xresults,
        std::chrono::system_clock::duration timeout = std::chrono::seconds(30));

    /// Read a message from the channel. If the message is a reply, try to
    /// match it with a pending call transaction and hand off a suitable
    /// XdrSource to that transaction. Returns true if a message was received
    /// otherwise false if the timeout was reached.
    bool receiveMessage(
        Transaction& tx, std::unique_lock<std::mutex>& lock,
        std::chrono::system_clock::duration timeout);

    /// Return an XDR to encode an outgoing message. When the message
    /// is complete, call endSend to send it to the remote endpoint.
    virtual std::unique_ptr<XdrSink> beginSend() = 0;

    /// Finish the current outgoing message and send to the remote
    /// endpoint if sendit is true. The caller must pass the message
    /// pointer returned by beginSend
    virtual void endSend(std::unique_ptr<XdrSink>&& msg, bool sendit) = 0;

    /// Return an XDR to decode an incoming message. When the message
    /// is decoded, call endReceive.
    virtual std::unique_ptr<XdrSource> beginReceive(
        std::chrono::system_clock::duration timeout) = 0;

    /// Signal to the derived class that we have finished processing
    /// the incoming message. If the message is not fully decoded, set
    /// skip to true, otherwise false. The caller must pass the
    /// message pointer returned by beginReceive
    virtual void endReceive(std::unique_ptr<XdrSource>&& msg, bool skip) = 0;

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
        std::unique_ptr<XdrSource> body;
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
    std::unique_ptr<XdrSink> beginSend() override;
    void endSend(std::unique_ptr<XdrSink>&& msg, bool sendit) override;
    std::unique_ptr<XdrSource> beginReceive(
        std::chrono::system_clock::duration timeout) override;
    void endReceive(std::unique_ptr<XdrSource>&& msg, bool skip) override;

private:
    size_t bufferSize_;
    std::deque<std::unique_ptr<XdrMemory>> queue_;
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
    std::unique_ptr<XdrSink> beginSend() override;
    void endSend(std::unique_ptr<XdrSink>&& msg, bool sendit) override;
    std::unique_ptr<XdrSource> beginReceive(
        std::chrono::system_clock::duration timeout) override;
    void endReceive(std::unique_ptr<XdrSource>&& msg, bool skip) override;

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
    std::unique_ptr<XdrSink> beginSend() override;
    void endSend(std::unique_ptr<XdrSink>&& msg, bool sendit) override;
    std::unique_ptr<XdrSource> beginReceive(
        std::chrono::system_clock::duration timeout) override;
    void endReceive(std::unique_ptr<XdrSource>&& msg, bool skip) override;

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
