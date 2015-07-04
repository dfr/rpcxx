// -*- c++ -*-

#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>

namespace oncrpc {

/// Given a Network ID (see RFC 5665), return a pair with the first element
/// the protocol family which implements the Network ID and the second element
/// the socket type to use.
std::pair<int, int> getNetId(const std::string& netid);

/// Similar to struct addrinfo but with C++ semantics for allocation
struct AddressInfo
{
    AddressInfo(addrinfo* ai);
    int flags;
    int family;
    int socktype;
    int protocol;
    int addrlen;
    sockaddr* addr;
    std::string canonname;
    sockaddr_storage storage;
};

std::vector<AddressInfo> getAddressInfo(
    const std::string& host, const std::string& service,
    const std::string& nettype);

class Socket;

class SocketManager
{
public:
    void add(std::shared_ptr<Socket> conn);

    void remove(std::shared_ptr<Socket> conn);

    void run();

    void stop();

private:
    std::mutex mutex_;
    bool stopping_ = false;
    std::unordered_map<int, std::shared_ptr<Socket>> sockets_;
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

protected:
    int fd_;
};

}
