// -*- c++ -*-

#pragma once

#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rpc++/rec.h>

namespace std {

template <>
class hash<std::pair<uint32_t, uint32_t>>
{
public:
    size_t operator()(const std::pair<uint32_t, uint32_t>& v) const
    {
        std::hash<uint32_t> h;
        return h(v.first) ^ h(v.second);
    }
};

}

namespace oncrpc {

class Connection;

typedef std::function<bool(uint32_t, XdrSource*, XdrSink*)> Service;

struct ServiceEntry
{
    Service handler;
    std::unordered_set<uint32_t> procs;
};

class ServiceRegistry
{
public:
    void add(
        uint32_t prog, uint32_t vers, ServiceEntry&& entry);

    void remove(uint32_t prog, uint32_t vers);
    
    const ServiceEntry* lookup(uint32_t prog, uint32_t vers) const;

    // Process an rpc message, returning true if there is a reply
    bool process(XdrSource* xdrin, XdrSink* xdrout);

private:
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> programs_;
    std::unordered_map<std::pair<uint32_t, uint32_t>, ServiceEntry> services_;
};

class ConnectionRegistry
{
public:
    void add(std::shared_ptr<Connection> conn);

    void remove(std::shared_ptr<Connection> conn);

    void run();

    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }

private:
    std::mutex mutex_;
    bool stopping_ = false;
    std::unordered_map<int, std::shared_ptr<Connection>> conns_;
};

/// A network connection of some kind with a socket file descriptor
class Connection
{
public:
    Connection(
        int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg);

    virtual ~Connection();

    int sock() const { return sock_; }

    /// Called from the connection registry when the connection is
    /// readable. Return true if the connection is still active or
    /// false if it should be removed from the registry.
    virtual bool onReadable(ConnectionRegistry* connreg) = 0;

protected:
    int sock_;
    size_t bufferSize_;
    std::shared_ptr<ServiceRegistry> svcreg_;
};

/// A connectionless datagram network connection
class DatagramConnection: public Connection
{
public:
    DatagramConnection(
        int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg);

    // Connection overrides
    bool onReadable(ConnectionRegistry* connreg) override;

private:
    std::vector<uint8_t> receivebuf_;
    std::vector<uint8_t> sendbuf_;
    std::unique_ptr<XdrMemory> dec_;
    std::unique_ptr<XdrMemory> enc_;
};

/// A stream-based network connection
class StreamConnection: public Connection
{
public:
    StreamConnection(
        int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg);

    // Connection overrides
    bool onReadable(ConnectionRegistry* connreg) override;

private:
    std::unique_ptr<RecordReader> dec_;
    std::unique_ptr<RecordWriter> enc_;
};

class ListenConnection: public Connection
{
public:
    ListenConnection(
        int sock, size_t bufferSize, std::shared_ptr<ServiceRegistry> svcreg);

    // Connection overrides
    bool onReadable(ConnectionRegistry* connreg) override;
};

}
