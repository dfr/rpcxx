// -*- c++ -*-

#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rpc++/rec.h>
#include <rpc++/rpcproto.h>
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
public:
    static std::chrono::seconds maxBackoff;

    /// Create an RPC channel
    Channel();

    ~Channel();

    /// Make a remote procedure call
    virtual void call(
        Client* client, uint32_t proc,
        std::function<void(XdrSink*)> xargs,
        std::function<void(XdrSource*)> xresults,
        std::chrono::system_clock::duration timeout = std::chrono::seconds(30));

    /// Return an XDR to encode an outgoing message. When the message
    /// is complete, call endCall to send it to the remote endpoint.
    virtual std::unique_ptr<XdrSink> beginCall() = 0;

    /// Finish the current outgoing message and send to the remote
    /// endpoint. The caller must pass the message pointer returned by
    /// beginCall
    virtual void endCall(std::unique_ptr<XdrSink>&& msg) = 0;

    /// Return an XDR to decode an incoming message. When the message
    /// is decoded, call endReply.
    virtual std::unique_ptr<XdrSource> beginReply(
        std::chrono::system_clock::duration timeout) = 0;

    /// Signal to the derived class that we have finished processing
    /// the incoming message. If the message is not fully decoded, set
    /// skip to true, otherwise false. The caller must pass the
    /// message pointer returned by beginReply
    virtual void endReply(std::unique_ptr<XdrSource>&& msg, bool skip) = 0;

    /// Close connection to server
    virtual void close() = 0;

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

        uint32_t seq;
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
};

/// Process RPC calls using the given registry of local
/// services. Thread safe.
class LocalChannel: public Channel
{
public:
    LocalChannel(std::shared_ptr<ServiceRegistry> svcreg);

    // Channel overrides
    std::unique_ptr<XdrSink> beginCall() override;
    void endCall(std::unique_ptr<XdrSink>&& msg) override;
    std::unique_ptr<XdrSource> beginReply(
        std::chrono::system_clock::duration timeout) override;
    void endReply(std::unique_ptr<XdrSource>&& msg, bool skip) override;
    void close() override;

private:
    size_t bufferSize_;
    std::shared_ptr<ServiceRegistry> svcreg_;
    std::deque<std::unique_ptr<XdrMemory>> queue_;
};

/// Send RPC messages over a socket
class SocketChannel: public Channel
{
public:
    SocketChannel(int sock);

    ~SocketChannel();

    /// Wait for the socket to become readable with the given
    /// timeout. Return true if the socket is readable or false if the
    /// timeout was reached.
    bool waitForReadable(std::chrono::system_clock::duration timeout);

    /// Return true if the socket is readable
    bool isReadable() const;

    /// Return true if the socket is writable
    bool isWritable() const;

    // Channel overrides
    void close() override;

protected:
    int sock_;
};

/// Send RPC messages over a connectionless datagram socket. Thread
/// safe.
class DatagramChannel: public SocketChannel
{
public:
    DatagramChannel(int sock);

    // Channel overrides
    std::unique_ptr<XdrSink> beginCall() override;
    void endCall(std::unique_ptr<XdrSink>&& msg) override;
    std::unique_ptr<XdrSource> beginReply(
        std::chrono::system_clock::duration timeout) override;
    void endReply(std::unique_ptr<XdrSource>&& msg, bool skip) override;

private:
    size_t bufferSize_;
    std::unique_ptr<XdrMemory> xdrs_;
};

/// Send RPC messages over a connected stream socket. Thread safe.
class StreamChannel: public SocketChannel
{
public:
    StreamChannel(int sock);

    ~StreamChannel();

    // Channel overrides
    std::unique_ptr<XdrSink> beginCall() override;
    void endCall(std::unique_ptr<XdrSink>&& msg) override;
    std::unique_ptr<XdrSource> beginReply(
        std::chrono::system_clock::duration timeout) override;
    void endReply(std::unique_ptr<XdrSource>&& msg, bool skip) override;

private:
    ptrdiff_t write(const void* buf, size_t len);

    size_t bufferSize_;

    // Protects sendbuf_ and sender_
    std::mutex writeMutex_;
    std::unique_ptr<XdrMemory> sendbuf_;
    std::unique_ptr<RecordWriter> sender_;
};

}
