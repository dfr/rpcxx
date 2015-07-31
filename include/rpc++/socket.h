// -*- c++ -*-

#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <rpc++/timeout.h>

namespace oncrpc {

/// Given a Network ID (see RFC 5665), return a pair with the first element
/// the protocol family which implements the Network ID and the second element
/// the socket type to use.
std::pair<int, int> getNetId(const std::string& netid);

/// Wrap a sockaddr
struct Address
{
public:
    Address()
    {
        addr_.ss_len = 0;
    }

    Address(const Address& other)
    {
        memcpy(&addr_, &other.addr_, other.addr_.ss_len);
    }

    Address(const std::string& path);

    Address(const sockaddr& sa)
    {
        memcpy(&addr_, &sa, sa.sa_len);
    }

    Address& operator=(const sockaddr& sa)
    {
        memcpy(&addr_, &sa, sa.sa_len);
        return *this;
    }

    operator bool() const
    {
        return addr_.ss_len > 0;
    }

    const sockaddr* addr() const
    {
        return reinterpret_cast<const sockaddr*>(&addr_);
    }

    sockaddr* addr()
    {
        return reinterpret_cast<sockaddr*>(&addr_);
    }

    socklen_t len() const
    {
        return addr_.ss_len;
    }

    socklen_t storageLen() const
    {
        return sizeof(addr_);
    }

    int operator==(const Address& other) const
    {
        return addr_.ss_len == other.addr_.ss_len &&
            memcmp(&addr_, &other.addr_, addr_.ss_len) == 0;
    }

private:
    sockaddr_storage addr_;
};

/// Similar to struct addrinfo but with C++ semantics for allocation
struct AddressInfo
{
    AddressInfo(addrinfo* ai);
    int flags;
    int family;
    int socktype;
    int protocol;
    Address addr;
    std::string canonname;
};

std::vector<AddressInfo> getAddressInfo(
    const std::string& host, const std::string& service,
    const std::string& nettype);

std::vector<AddressInfo> getAddressInfo(
    const std::string& url,
    const std::string& nettype);

AddressInfo uaddr2taddr(
    const std::string& uaddr, const std::string& netid);

class Socket;

class SocketManager: public TimeoutManager
{
public:
    SocketManager();
    ~SocketManager();

    void add(std::shared_ptr<Socket> conn);

    void remove(std::shared_ptr<Socket> conn);

    void run();

    void stop();

    // TimeoutManager overrides
    task_type add(
        clock_type::time_point when, std::function<void()> what) override;

private:
    std::mutex mutex_;
    bool running_ = false;
    bool stopping_ = false;
    std::unordered_map<int, std::shared_ptr<Socket>> sockets_;
    int pipefds_[2];
};

struct Socket
{
public:
    Socket(int fd)
        : fd_(fd)
    {
    }

    virtual ~Socket();

    /// Wait for the socket to become readable with the given
    /// timeout. Return true if the socket is readable or false if the
    /// timeout was reached.
    bool waitForReadable(std::chrono::system_clock::duration timeout);

    /// Return true if the socket is readable
    bool isReadable() const;

    /// Return true if the socket is writable
    bool isWritable() const;

    /// Close the socket
    void close();

    /// Return the OS file descriptor for the socket
    int fd() const { return fd_; }

    /// Called from SocketManager::run when the socket is readable. Return
    /// true if the socket is still active or false if it should be closed
    virtual bool onReadable(SocketManager* sockman) = 0;

    /// Bind the local address
    virtual void bind(const Address& addr)
    {
        if (::bind(fd_, addr.addr(), addr.len()) < 0)
            throw std::system_error(errno, std::system_category());
    }

    /// Connect the socker to a remote address
    virtual void connect(const Address& addr)
    {
        if (::connect(fd_, addr.addr(), addr.len()) < 0)
            throw std::system_error(errno, std::system_category());
    }

    auto send(const void* buf, size_t buflen)
    {
        auto len = ::send(fd_, buf, buflen, 0);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    auto sendto(const void* buf, size_t buflen, const Address& addr)
    {
        auto len = ::sendto(fd_, buf, buflen, 0, addr.addr(), addr.len());
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    auto recv(void* buf, size_t buflen)
    {
        auto len = ::recv(fd_, buf, buflen, 0);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    auto recvfrom(void* buf, size_t buflen, Address& addr)
    {
        socklen_t alen = addr.storageLen();
        auto len = ::recvfrom(fd_, buf, buflen, 0, addr.addr(), &alen);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

protected:
    int fd_;
};

}
