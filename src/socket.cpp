/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <glog/logging.h>

#include <rpc++/errors.h>
#include <rpc++/socket.h>
#include <rpc++/urlparser.h>

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

Address::Address(const Address& other)
{
    copyFrom(reinterpret_cast<const sockaddr&>(other.addr_));
}

Address::Address(const sockaddr& sa)
{
    copyFrom(sa);
}

Address::Address(const std::string& host)
{
    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    int err = ::getaddrinfo(host.c_str(), nullptr, &hints, &res0);
    if (err) {
        std::ostringstream msg;
        msg << "RPC: " << host << ":" << gai_strerror(err);
        throw RpcError(msg.str());
    }
    *this = *res0->ai_addr;
    ::freeaddrinfo(res0);
}

Address Address::fromUrl(const std::string& url)
{
    UrlParser p(url);
    if (p.scheme == "unix") {
        Address res;
        sockaddr_un& sun = reinterpret_cast<sockaddr_un&>(res.addr_);
        sun.sun_family = AF_LOCAL;
        strlcpy(sun.sun_path, p.path.c_str(), sizeof(sun.sun_path));
        sun.sun_len = SUN_LEN(&sun);
        return res;
    }
    else if (p.scheme == "tcp" || p.scheme == "udp") {
        auto host = p.host;
        auto port = p.port.size() > 0 ? p.port : "0";

        addrinfo hints;
        addrinfo* addrs;
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = 0;
        hints.ai_family = PF_UNSPEC;
        if (std::isdigit(p.host[0])) {
            hints.ai_flags = AI_NUMERICHOST;
            hints.ai_family = PF_INET;
        }
        else if (p.host[0] == '[') {
            hints.ai_flags = AI_NUMERICHOST;
            hints.ai_family = PF_INET6;
            host = host.substr(1, host.size() - 2);
        }
        hints.ai_socktype = p.scheme == "tcp" ? SOCK_STREAM : SOCK_DGRAM;
        int err = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addrs);
        if (err)
            throw RpcError(std::string("getaddrinfo: ") + gai_strerror(err));
        Address res(*addrs->ai_addr);
        ::freeaddrinfo(addrs);
        return res;
    }
    else {
        throw RpcError("unsupported scheme: " + p.scheme);
    }
}

Address& Address::operator=(const sockaddr& sa)
{
    copyFrom(sa);
    return *this;
}

std::string Address::host() const
{
    char buf[512];
    int err = ::getnameinfo(
        addr(), len(), buf, sizeof(buf), nullptr, 0, 0);
    if (err) {
        err = ::getnameinfo(
            addr(), len(), buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST);
        if (err)
            return "unknown";
    }
    return buf;
}

std::string Address::uaddr() const
{
    std::ostringstream ss;
    auto family = addr()->sa_family;
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr();
        uint16_t* p = (uint16_t*) sin6p->sin6_addr.s6_addr;
        std::vector<uint16_t> head, tail;
        int i;
        for (i = 0; i < 8; i++) {
            if (p[i])
                head.push_back(ntohs(p[i]));
            else
                break;
        }
        for (; i < 8 && p[i] == 0; i++)
            ;
        for (; i < 8; i++)
            tail.push_back(ntohs(p[i]));
        if (head.size() + tail.size() == 0) {
            ss << "::";
        }
        else {
            bool first = true;
            ss << std::hex;
            for (auto val: head) {
                if (!first)
                    ss << ":";
                first = false;
                ss << val;
            }
            if (head.size() + tail.size() < 8) {
                ss << ":";
                first = false;
            }
            for (auto val: tail) {
                if (!first)
                    ss << ":";
                first = false;
                ss << val;
            }
        }
        uint16_t port = ntohs(sin6p->sin6_port);
        ss << std::dec << "." << (port >> 8) << "." << (port & 0xff);
    }
    else {
        sockaddr_in* sinp = (sockaddr_in*) addr();
        uint8_t* p = (uint8_t*) &sinp->sin_addr;
        ss << int(p[0]) << "."
           << int(p[1]) << "."
           << int(p[2]) << "."
           << int(p[3]) << ".";
        p = (uint8_t*) &sinp->sin_port;
        ss << int(p[0]) << "."
           << int(p[1]);
    }
    return ss.str();
}

int Address::port() const
{
    auto family = addr()->sa_family;
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr();
        return ntohs(sin6p->sin6_port);
    }
    else if (family == AF_INET) {
        sockaddr_in* sinp = (sockaddr_in*) addr();
        return ntohs(sinp->sin_port);
    }
    return -1;
}

void Address::setPort(int val)
{
    auto family = addr()->sa_family;
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr();
        sin6p->sin6_port = htons(val);
    }
    else if (family == AF_INET) {
        sockaddr_in* sinp = (sockaddr_in*) addr();
        sinp->sin_port = htons(val);
    }
}

bool Address::isWildcard() const
{
    auto family = addr()->sa_family;
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr();
        return memcmp(&sin6p->sin6_addr, &in6addr_any, sizeof(in6_addr)) == 0;
    }
    else if (family == AF_INET) {
        sockaddr_in* sinp = (sockaddr_in*) addr();
        return sinp->sin_addr.s_addr == INADDR_ANY;
    }
    return false;
}

void Address::copyFrom(const sockaddr& sa)
{
    memcpy(&addr_, &sa, sa.sa_len);
    if (sa.sa_family == AF_LOCAL) {
        // Try to ensure the path is null terminated
        if (sa.sa_len < sizeof(sockaddr_un))
            reinterpret_cast<char*>(&addr_)[sa.sa_len] = 0;
    }
}

Network::Network(const std::string& addr)
{
    auto slash = addr.find('/');
    auto host = slash == std::string::npos ? addr : addr.substr(0, slash);

    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    int err = ::getaddrinfo(host.c_str(), nullptr, &hints, &res0);
    if (err) {
        std::ostringstream msg;
        msg << "RPC: " << host << ":" << gai_strerror(err);
        throw RpcError(msg.str());
    }
    addr_ = Address(*res0->ai_addr);

    if (slash != std::string::npos) {
        prefix_ = stoi(addr.substr(slash + 1));
    }
    else {
        if (res0->ai_addr->sa_family == AF_INET)
            prefix_ = 32;
        else if (res0->ai_addr->sa_family == AF_INET6)
            prefix_ = 128;
        else {
            ::freeaddrinfo(res0);
            throw RpcError("unexpected address family");
        }
    }

    ::freeaddrinfo(res0);
}

bool Network::matches(const Address& addr)
{
    if (addr_.addr()->sa_family != addr.addr()->sa_family)
        return false;
    if (addr_.addr()->sa_len != addr.addr()->sa_len)
        return false;
    uint8_t* p;
    uint8_t* q;
    int len;
    if (addr_.addr()->sa_family == PF_INET) {
        p = (uint8_t*) &((sockaddr_in*) addr_.addr())->sin_addr;
        q = (uint8_t*) &((sockaddr_in*) addr.addr())->sin_addr;
        len = 4;
    }
    else if (addr_.addr()->sa_family == PF_INET6) {
        p = (uint8_t*) &((sockaddr_in6*) addr_.addr())->sin6_addr;
        q = (uint8_t*) &((sockaddr_in6*) addr.addr())->sin6_addr;
        len = 16;
    }
    else
        return false;
    auto prefix = prefix_;

    // This assumes that network addresses are stored big-endian which
    // is true for IPv4 and IPV6
    while (len > 0 && prefix > 0) {
        if (prefix >= 8) {
            if (*p != *q)
                return false;
        }
        else {
            int mask = (0xff >> prefix) << (8 - prefix);
            if ((*p & mask) != (*q & mask))
                return false;
        }
        p++;
        q++;
        len--;
        prefix -= 8;
    }
    return true;
}

bool Filter::check(const Address& addr)
{
    bool accepted = allowed_.size() == 0;
    for (auto& net: allowed_) {
        if (net.matches(addr)) {
            accepted = true;
            break;
        }
    }
    for (auto& net: denied_) {
        if (net.matches(addr)) {
            accepted = false;
            break;
        }
    }
    return accepted;
}

AddressInfo::AddressInfo(addrinfo* ai)
    : flags(ai->ai_flags),
      family(ai->ai_family),
      socktype(ai->ai_socktype),
      protocol(ai->ai_protocol),
      addr(*ai->ai_addr),
      canonname(ai->ai_canonname ? ai->ai_canonname : "")
{
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

std::vector<AddressInfo> oncrpc::getAddressInfo(
    const std::string& url, const std::string& netid)
{
    UrlParser p(url);

    if (p.scheme == "local") {
        assert(netid == "" || netid == "local");
        // Special case for local sockets
        sockaddr_un sun;
        sun.sun_family = AF_LOCAL;
        std::copy_n(p.path.data(), p.path.size(), sun.sun_path);
        sun.sun_path[p.path.size()] = '\0';
        sun.sun_len = SUN_LEN(&sun);
        AddressInfo ai;
        ai.flags = 0;
        ai.family = PF_LOCAL;
        ai.socktype = SOCK_STREAM;
        ai.protocol = 0;
        ai.addr = Address(reinterpret_cast<sockaddr&>(sun));
        std::vector<AddressInfo> res { ai };
        return res;
    }

    std::pair<int, int> nt;
    if (netid == "") {
        // Guess based on url scheme
        if (p.scheme == "udp")
            nt = getNetId("udp");
        else
            nt = getNetId("tcp");
    }
    else {
        nt = getNetId(netid);
    }

    auto host = p.host;
    auto service = p.port.size() > 0 ? p.port : p.scheme;
    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = std::get<0>(nt);
    hints.ai_socktype = std::get<1>(nt);
    if (std::isdigit(p.host[0])) {
        hints.ai_flags = AI_NUMERICHOST;
        hints.ai_family = PF_INET;
    }
    else if (p.host[0] == '[') {
        hints.ai_flags = AI_NUMERICHOST;
        hints.ai_family = PF_INET6;
        host = host.substr(1, host.size() - 2);
    }
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

std::string AddressInfo::host() const
{
    return addr.host();
}

std::string AddressInfo::uaddr() const
{
    return addr.uaddr();
}

std::string AddressInfo::netid() const
{
    if (family == AF_INET6) {
        if (socktype == SOCK_STREAM)
            return "tcp6";
        else if (socktype == SOCK_DGRAM)
            return "udp6";
    }
    else if (family == AF_INET) {
        if (socktype == SOCK_STREAM)
            return "tcp";
        else if (socktype == SOCK_DGRAM)
            return "udp";
    }
    return "";
}

int AddressInfo::port() const
{
    return addr.port();
}

void AddressInfo::setPort(int val)
{
    addr.setPort(val);
}

bool AddressInfo::isWildcard() const
{
    return addr.isWildcard();
}

AddressInfo AddressInfo::fromUaddr(
    const std::string& uaddr, const std::string& netid)
{
    auto portloIndex = uaddr.rfind('.');
    if (portloIndex == std::string::npos)
        throw RpcError(
            "malformed address from remote rpcbind: " + uaddr);
    auto porthiIndex = uaddr.rfind('.', portloIndex - 1);
    if (porthiIndex == std::string::npos)
        throw RpcError(
            "malformed address from remote rpcbind: " + uaddr);
    auto host = uaddr.substr(0, porthiIndex);
    auto porthi = std::stoi(
        uaddr.substr(porthiIndex + 1, portloIndex - porthiIndex - 1));
    auto portlo = std::stoi(uaddr.substr(portloIndex + 1));
    auto port = (porthi << 8) + portlo;

    addrinfo hints;
    addrinfo* addrs;
    memset(&hints, 0, sizeof(hints));
    auto nt = getNetId(netid);
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = std::get<0>(nt);
    hints.ai_socktype = std::get<1>(nt);
    std::string service = std::to_string(port);

    int err = getaddrinfo(host.c_str(), service.c_str(), &hints, &addrs);
    if (err)
        throw RpcError(std::string("getaddrinfo: ") + gai_strerror(err));

    AddressInfo ai(addrs);
    freeaddrinfo(addrs);
    return ai;
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

void
Socket::setFd(int fd)
{
    fd_ = fd;
}

void
Socket::bind(const Address& addr)
{
    auto family = addr.addr()->sa_family;
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr.addr();
        if (IN6_IS_ADDR_MULTICAST(&sin6p->sin6_addr)) {
            struct ipv6_mreq mreq;
            mreq.ipv6mr_multiaddr = sin6p->sin6_addr;
            mreq.ipv6mr_interface = 0;
            auto res = ::setsockopt(
                fd_, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
            if (res < 0)
                throw std::system_error(errno, std::system_category());
        }
    }
    else if (family == AF_INET) {
        sockaddr_in* sinp = (sockaddr_in*) addr.addr();
        if (IN_MULTICAST(ntohl(sinp->sin_addr.s_addr))) {
            struct ip_mreq mreq;
            mreq.imr_multiaddr = sinp->sin_addr;
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            auto res = ::setsockopt(
                fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
            if (res < 0)
                throw std::system_error(errno, std::system_category());
        }
    }

    if (::bind(fd_, addr.addr(), addr.len()) < 0)
        throw std::system_error(errno, std::system_category());
}
