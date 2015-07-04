#include <cstdlib>
#include <sstream>

#include <unistd.h>

#include <rpc++/socket.h>
#include <rpc++/util.h>

using namespace oncrpc;

std::pair<int, int> oncrpc::getNetId(const std::string& netid)
{
    if (netid == "tcp")
        return std::make_pair(PF_UNSPEC, SOCK_STREAM);
    if (netid == "udp")
        return std::make_pair(PF_UNSPEC, SOCK_DGRAM);

    if (netid == "tcp6")
        return std::make_pair(PF_INET6, SOCK_STREAM);
    if (netid == "udp6")
        return std::make_pair(PF_INET6, SOCK_DGRAM);

    throw RpcError("Bad netid");
}

AddressInfo::AddressInfo(addrinfo* ai)
    : flags(ai->ai_flags),
      family(ai->ai_family),
      socktype(ai->ai_socktype),
      protocol(ai->ai_protocol),
      addrlen(ai->ai_addrlen),
      addr(reinterpret_cast<sockaddr*>(&storage)),
      canonname(ai->ai_canonname ? ai->ai_canonname : "")
{
    memcpy(addr, ai->ai_addr, ai->ai_addrlen);
}

std::vector<AddressInfo> oncrpc::getAddressInfo(
    const std::string& host, const std::string& service,
    const std::string& netid)
{
    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    auto nt = getNetId(netid);
    hints.ai_family = std::get<0>(nt);
    hints.ai_socktype = std::get<1>(nt);
    int err = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &res0);
    if (err) {
        std::ostringstream msg;
        msg << "RPC: " << host << ":" << service << ": " << gai_strerror(err);
        throw RpcError(msg.str());
    }

    std::vector<AddressInfo> addrs;
    for (addrinfo* res = res0; res; res = res->ai_next)
        addrs.emplace_back(res);
    ::freeaddrinfo(res0);

    return addrs;
}

void
SocketManager::add(std::shared_ptr<Socket> sock)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sockets_[sock->fd()] = sock;
}

void
SocketManager::remove(std::shared_ptr<Socket> sock)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sockets_.erase(sock->fd());
}

void
SocketManager::run()
{
    mutex_.lock();
    while (sockets_.size() > 0 && !stopping_) {
        fd_set rset;
        int maxfd = 0;
        FD_ZERO(&rset);
        for (const auto& i: sockets_) {
            int fd = i.first;
            maxfd = std::max(maxfd, fd);
            FD_SET(fd, &rset);
        }
        mutex_.unlock();
        auto nready = ::select(maxfd + 1, &rset, nullptr, nullptr, nullptr);
        if (nready < 0) {
            throw std::system_error(errno, std::system_category());
        }

        if (nready == 0)
            continue;

        mutex_.lock();
        std::vector<std::shared_ptr<Socket>> ready;
        for (const auto& i: sockets_) {
            if (FD_ISSET(i.first, &rset)) {
                ready.push_back(i.second);
            }
        }
        mutex_.unlock();

        for (auto sock: ready) {
            if (!sock->onReadable(this))
                remove(sock);
        }

        mutex_.lock();
    }
    mutex_.unlock();
}

void
SocketManager::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
}

Socket::~Socket()
{
    close();
}

bool
Socket::waitForReadable(std::chrono::system_clock::duration timeout)
{
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd_, &rset);

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(timeout);
    struct timeval tv {
        static_cast<int>(us.count() / 1000000),
        static_cast<int>(us.count() % 1000000)
    };

    auto nready = ::select(fd_ + 1, &rset, nullptr, nullptr, &tv);
    if (nready <= 0)
        return false;
    return true;
}

bool
Socket::isReadable() const
{
    fd_set rset;
    struct timeval tv { 0, 0 };

    FD_ZERO(&rset);
    FD_SET(fd_, &rset);
    auto nready = ::select(fd_ + 1, &rset, nullptr, nullptr, &tv);
    return nready == 1;
}

bool
Socket::isWritable() const
{
    fd_set wset;
    struct timeval tv { 0, 0 };

    FD_ZERO(&wset);
    FD_SET(fd_, &wset);
    auto nready = ::select(fd_ + 1, nullptr, &wset, nullptr, &tv);
    return nready == 1;
}

void
Socket::close()
{
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}
