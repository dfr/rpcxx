/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

// -*- c++ -*-

#pragma once

#include <chrono>
#include <string>
#include <system_error>
#include <vector>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

namespace oncrpc {

/// Given a Network ID (see RFC 5665), return a pair with the first element
/// the protocol family which implements the Network ID and the second element
/// the socket type to use.
std::pair<int, int> getNetId(const std::string& netid);

/// Wrap a sockaddr
class Address
{
public:
    Address()
    {
        addr_.ss_len = 0;
    }

    Address(const Address& other);
    Address(const sockaddr& sa);
    explicit Address(const std::string& host);

    static Address fromUrl(const std::string& url);

    Address& operator=(const sockaddr& sa);

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

    std::string host() const;
    std::string uaddr() const;
    int port() const;
    void setPort(int val);
    bool isWildcard() const;

private:
    void copyFrom(const sockaddr& sa);

    sockaddr_storage addr_;
};

/// A representation of a network's address range using a base address
/// and prefix length.
class Network
{
public:
    Network()
        : prefix_(0)
    {
    }

    Network(const Network& net)
        : addr_(net.addr_),
          prefix_(net.prefix_)
    {
    }

    Network(const Address& addr, int prefix)
        : addr_(addr),
          prefix_(prefix)
    {
    }

    explicit Network(const std::string& addr);

    auto& addr() const { return addr_; }
    auto prefix() const { return prefix_; }

    /// Return true if the given address matches this network prefix
    bool matches(const Address& addr);

private:
    Address addr_;
    int prefix_;
};

/// A utility class which can be used to filter requests based on the
/// source address. The filter has two lists of networks, one for
/// networks which are allowed to send requests and one for networks
/// which are not.
///
/// An incoming request is first matched against the allow list - if
/// the list is empty or the source address matches an entry in the
/// list, the request is then checked against the deny list. If no
/// entry in the deny list matches, the request is accepted. All other
/// requests are rejected.
class Filter
{
public:
    Filter()
    {
    }

    Filter(
        std::initializer_list<Network> allowed,
        std::initializer_list<Network> denied)
        : allowed_(allowed),
          denied_(denied)
    {
    }

    /// Add a network which is allowed access. If no networks are
    /// explicitly allowed, any request is allowed.
    void allow(const Network& net)
    {
        allowed_.push_back(net);
    }

    /// Add a network which is denied access. If no
    /// networks are explicitly denied, no requests are denied.
    void deny(const Network& net)
    {
        denied_.push_back(net);
    }

    /// Return true if a request from the given address is accepted by
    /// the filter
    bool check(const Address& addr);

private:
    std::vector<Network> allowed_;
    std::vector<Network> denied_;
};

/// Similar to struct addrinfo but with C++ semantics for allocation
struct AddressInfo
{
    AddressInfo()
        : flags(0),
          family(0),
          socktype(0),
          protocol(0)
    {
    }

    AddressInfo(addrinfo* ai);
    int flags;
    int family;
    int socktype;
    int protocol;
    Address addr;
    std::string canonname;

    static AddressInfo fromUaddr(
        const std::string& uaddr, const std::string& netid);

    std::string host() const;
    std::string uaddr() const;
    std::string netid() const;
    int port() const;
    void setPort(int val);
    bool isWildcard() const;
};

std::vector<AddressInfo> getAddressInfo(
    const std::string& host, const std::string& service,
    const std::string& nettype);

std::vector<AddressInfo> getAddressInfo(
    const std::string& url,
    const std::string& nettype = "");

class SocketManager;

class Socket
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

    /// Return the close-on-idle flag for the socket
    bool closeOnIdle() const { return closeOnIdle_; }

    /// Set the close-on-idle flag for the socket
    virtual void setCloseOnIdle(bool closeOnIdle)
    {
	closeOnIdle_ = closeOnIdle;
    }

    /// Return the OS file descriptor for the socket
    int fd() const { return fd_; }

    /// Change the socket file descriptor (e.g. when reconnecting a
    /// socket)
    void setFd(int fd);

    /// Called from SocketManager::run when the socket is readable. Return
    /// true if the socket is still active or false if it should be closed
    virtual bool onReadable(SocketManager* sockman) { return false; }

    /// Bind the local address
    virtual void bind(const Address& addr);

    /// Listen for connections
    virtual void listen()
    {
        if (::listen(fd_, SOMAXCONN) < 0)
            throw std::system_error(errno, std::system_category());
    }

    /// Connect the socker to a remote address
    virtual void connect(const Address& addr)
    {
        if (::connect(fd_, addr.addr(), addr.len()) < 0)
            throw std::system_error(errno, std::system_category());
    }

    virtual ssize_t send(const void* buf, size_t buflen)
    {
        auto len = ::send(fd_, buf, buflen, 0);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    virtual ssize_t send(const std::vector<iovec>& iov)
    {
        auto len = ::writev(fd_, iov.data(), iov.size());
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    virtual ssize_t sendto(const void* buf, size_t buflen, const Address& addr)
    {
        auto len = ::sendto(fd_, buf, buflen, 0, addr.addr(), addr.len());
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    virtual ssize_t sendto(const std::vector<iovec>& iov, const Address& addr)
    {
        msghdr mh;
        mh.msg_name = const_cast<sockaddr*>(addr.addr());
        mh.msg_namelen = addr.len();
        mh.msg_iov = const_cast<iovec*>(iov.data());
        mh.msg_iovlen = iov.size();
        mh.msg_control = nullptr;
        mh.msg_controllen = 0;
        mh.msg_flags = 0;
        auto len = ::sendmsg(fd_, &mh, 0);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    virtual ssize_t recv(void* buf, size_t buflen)
    {
        auto len = ::recv(fd_, buf, buflen, 0);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    virtual ssize_t recvfrom(void* buf, size_t buflen, Address& addr)
    {
        socklen_t alen = addr.storageLen();
        auto len = ::recvfrom(fd_, buf, buflen, 0, addr.addr(), &alen);
        if (len < 0)
            throw std::system_error(errno, std::system_category());
        return len;
    }

    Address peerName() const
    {
        struct sockaddr_storage ss;
        socklen_t len = sizeof(ss);
        if (::getpeername(
                fd_, reinterpret_cast<sockaddr*>(&ss), &len) < 0) {
            throw std::system_error(errno, std::system_category());
        }
        return Address(reinterpret_cast<const sockaddr&>(ss));
    }

    auto owner() const { return owner_.lock(); }

    void setOwner(std::shared_ptr<SocketManager> owner)
    {
        owner_ = owner;
    }

private:
    int fd_;
    bool closeOnIdle_ = false;
    std::weak_ptr<SocketManager> owner_;
};

}
