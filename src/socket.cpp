#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <glog/logging.h>

#include <rpc++/errors.h>
#include <rpc++/socket.h>

using namespace oncrpc;

namespace {

struct UrlParser
{
    UrlParser(const std::string& url)
    {
        std::string s = url;
        parseScheme(s);
        schemeSpecific = s;
        if (isHostbased()) {
            if (s.substr(0, 2) != "//")
                throw RpcError("malformed url");
            s = s.substr(2);
            parseHost(s);
            if (s[0] == ':') {
                s = s.substr(1);
                parsePort(s);
            }
        }
        else if (scheme == "unix") {
            if (s.substr(0, 2) != "//")
                throw RpcError("malformed url");
            path = s.substr(2);
        }
    }

    bool isHostbased()
    {
        return scheme == "tcp" || scheme == "udp" || scheme == "http" ||
            scheme == "https" || scheme == "nfs";
    }

    void parseScheme(std::string& s)
    {
        scheme = "";
        if (!std::isalpha(s[0]))
            throw RpcError("malformed url");
        while (s.size() > 0 && s[0] != ':') {
	    auto ch = s[0];
            if (!std::isalnum(ch) && ch != '+' && ch != '.' && ch != '-')
                throw std::runtime_error("malformed url");
            scheme += ch;
            s = s.substr(1);
        }
        if (s.size() == 0 || s[0] != ':')
            throw RpcError("malformed url");
        s = s.substr(1);
    }

    void parseHost(std::string& s)
    {
        if (s.size() == 0)
            return;
        if (std::isdigit(s[0])) {
            parseIPv4(s);
        }
        else if (s[0] == '[') {
            parseIPv6(s);
        }
        else {
            int i = 0;
            while (i < s.size() && s[i] != ':' && s[i] != '/')
                i++;
            host = s.substr(0, i);
            s = s.substr(i);
        }
    }

    void parseIPv4(std::string& s)
    {
        host = "";
        for (int i = 0; i < 4; i++) {
            if (s.size() == 0)
                throw RpcError("malformed IPv4 address");
            if (i > 0) {
                if (s[0] != '.')
                    throw RpcError("malformed IPv4 address");
                host += '.';
                s = s.substr(1);
                if (s.size() == 0)
                    throw RpcError("malformed IPv4 address");
            }
            while (s.size() > 0 && std::isdigit(s[0])) {
                host += s[0];
                s = s.substr(1);
            }
        }
    }

    void parseIPv6(std::string& s)
    {
        std::vector<std::uint16_t> parts;

        host = '[';
        auto i = s.find(']');
        if (i == std::string::npos)
            throw RpcError("malformed IPv6 address");
        auto t = s.substr(1, i - 1);
        s = s.substr(i + 1);
        while (t.size() > 0 &&
               (std::isxdigit(t[0]) || t[0] == ':' || t[0] == '.')) {
            host += t[0];
            t = t.substr(1);
        }
        if (t.size() != 0)
            throw RpcError("malformed IPv6 address");
        host += ']';
    }

    void parsePort(std::string& s)
    {
        port = "";
        while (s.size() > 0 && std::isdigit(s[0])) {
            port += s[0];
            s = s.substr(1);
        }
    }

    std::string scheme;
    std::string schemeSpecific;
    std::string host;
    std::string port;
    std::string path;
};

}

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

Address::Address(const std::string& url)
{
    UrlParser p(url);
    if (p.scheme == "unix") {
        auto& sun = reinterpret_cast<sockaddr_un&>(addr_);
        sun.sun_family = AF_LOCAL;
        strlcpy(sun.sun_path, p.path.c_str(), sizeof(sun.sun_path));
        sun.sun_len = SUN_LEN(&sun);
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
        int err = getaddrinfo(host.c_str(), port.c_str(), &hints, &addrs);
        if (err)
            throw RpcError(std::string("getaddrinfo: ") + gai_strerror(err));
        *this = *addrs->ai_addr;
        freeaddrinfo(addrs);
    }
    else {
        throw RpcError("unsupported scheme: " + p.scheme);
    }
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
    if (netid == "local") {
        // Special case for local sockets
        sockaddr_un sun;
        sun.sun_family = AF_LOCAL;
        std::copy_n(url.data(), url.size(), sun.sun_path);
        sun.sun_path[url.size()] = '\0';
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

    UrlParser p(url);

    auto host = p.host;
    auto service = p.port.size() > 0 ? p.port : p.scheme;
    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    auto nt = getNetId(netid);
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

std::string AddressInfo::uaddr() const
{
    std::ostringstream ss;
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr.addr();
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
        bool first = true;
        ss << std::hex;
        for (auto val: head) {
            if (!first)
                ss << ":";
            first = false;
            ss << val;
        }
        if (head.size() + tail.size() < 8) {
            if (first)
                ss << ":";
            ss << ":";
            first = false;
        }
        for (auto val: head) {
            if (!first)
                ss << ":";
            first = false;
            ss << val;
        }
        uint16_t port = ntohs(sin6p->sin6_port);
        ss << std::dec << "." << (port >> 8) << "." << (port & 0xff);
    }
    else {
        sockaddr_in* sinp = (sockaddr_in*) addr.addr();
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
    if (family == AF_INET6) {
        sockaddr_in6* sin6p = (sockaddr_in6*) addr.addr();
        return ntohs(sin6p->sin6_port);
    }
    else if (family == AF_INET) {
        sockaddr_in* sinp = (sockaddr_in*) addr.addr();
        return ntohs(sinp->sin_port);
    }
    return -1;
}

AddressInfo oncrpc::uaddr2taddr(
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

SocketManager::SocketManager()
    : idleTimeout_(std::chrono::seconds(30))
{
    ::pipe(pipefds_);
}

SocketManager::~SocketManager()
{
    ::close(pipefds_[0]);
    ::close(pipefds_[1]);
}

void
SocketManager::add(std::shared_ptr<Socket> sock)
{
    VLOG(3) << "adding socket " << sock;
    std::unique_lock<std::mutex> lock(mutex_);
    sockets_[sock] = clock_type::now();
}

void
SocketManager::remove(std::shared_ptr<Socket> sock)
{
    VLOG(3) << "removing socket " << sock;
    std::unique_lock<std::mutex> lock(mutex_);
    sockets_.erase(sock);
}

void
SocketManager::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    running_ = true;
    while (sockets_.size() > 0 && !stopping_) {
        auto idleLimit = clock_type::now() - idleTimeout_;
        std::vector<std::shared_ptr<Socket>> idle;
        fd_set rset;
        int maxfd = pipefds_[0];
        FD_ZERO(&rset);
        FD_SET(pipefds_[0], &rset);
        for (const auto& i: sockets_) {
            if (i.first->closeOnIdle() &&
		i.second < idleLimit) {
                VLOG(3) << "idle timeout for socket " << i.first->fd();
                idle.push_back(i.first);
                continue;
            }
            int fd = i.first->fd();
            maxfd = std::max(maxfd, fd);
            FD_SET(fd, &rset);
        }
        lock.unlock();

        for (auto sock: idle) {
            remove(sock);
        }
        idle.clear();

        auto stopTime = next();
        auto now = clock_type::now();
        auto timeout = std::chrono::duration_cast<std::chrono::microseconds>(
            stopTime - now);
        if (timeout > idleTimeout_)
            timeout = idleTimeout_;
        if (timeout.count() < 0)
            timeout = std::chrono::seconds(0);
        auto sec = timeout.count() / 1000000;
        auto usec = timeout.count() % 1000000;
        if (sec > 999999)
            sec = 999999; //std::numeric_limits<int>::max();
        VLOG(3) << "sleeping for " << sec << "." << usec << "s";
        ::timeval tv { int(sec), int(usec) };
        auto nready = ::select(maxfd + 1, &rset, nullptr, nullptr, &tv);
        if (nready < 0) {
            throw std::system_error(errno, std::system_category());
        }

        // Execute timeouts, if any
        now = clock_type::now();
        update(now);

        lock.lock();
        if (nready == 0) {
            continue;
        }

        if (FD_ISSET(pipefds_[0], &rset)) {
            char ch;
            ::read(pipefds_[0], &ch, 1);
            continue;
        }

        std::vector<std::shared_ptr<Socket>> ready;
        for (auto& i: sockets_) {
            if (FD_ISSET(i.first->fd(), &rset)) {
                i.second = now;
                ready.push_back(i.first);
            }
        }
        lock.unlock();

        for (auto sock: ready) {
            if (!sock->onReadable(this))
                remove(sock);
        }

        lock.lock();
    }
    running_ = false;
}

void
SocketManager::stop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    stopping_ = true;
    char ch = 0;
    ::write(pipefds_[1], &ch, 1);
}

TimeoutManager::task_type
SocketManager::add(
    clock_type::time_point when, std::function<void()> what)
{
    auto tid = TimeoutManager::add(when, what);
    std::unique_lock<std::mutex> lock(mutex_);
    if (running_) {
        char ch = 0;
        ::write(pipefds_[1], &ch, 1);
    }
    return tid;
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
