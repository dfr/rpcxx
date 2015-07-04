#include <sstream>

#include <unistd.h>

#include <rpc++/channel.h>
#include <rpc++/rpcbind.h>
#include <rpc++/util.h>

namespace oncrpc {

std::unique_ptr<AddressInfo> uaddr2taddr(
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

    auto res = std::make_unique<AddressInfo>(addrs);
    freeaddrinfo(addrs);
    return std::move(res);
}

int connectSocket(
    const std::string& host, const std::string& service,
    const std::string& netid)
{
    auto addrs = getAddressInfo(host, service, netid);

    int s = -1;
    std::string cause = "";
    std::error_code ec;
    bool isStream = false;
    for (const auto& addr: addrs) {
        isStream = addr.socktype == SOCK_STREAM;
        s = ::socket(addr.family, addr.socktype, addr.protocol);
        if (s < 0) {
            cause = "socket";
            ec = std::error_code(errno, std::system_category());
            continue;
        }

        if (::connect(s, addr.addr, addr.addrlen) < 0) {
            cause = "connect";
            ec = std::error_code(errno, std::system_category());
            ::close(s);
            s = -1;
            continue;
        }

        break;
    }

    if (s == -1)
        throw std::system_error(ec);

    return s;
}

std::shared_ptr<Channel> connectChannel(std::unique_ptr<AddressInfo>&& addr)
{
    std::string cause = "";
    std::error_code ec;
    int s;

    s = socket(addr->family, addr->socktype, addr->protocol);
    if (s < 0) {
        cause = "socket";
        ec = std::error_code(errno, std::system_category());
    }
    else {
        if (connect(s, addr->addr, addr->addrlen) < 0) {
            cause = "connect";
            ec = std::error_code(errno, std::system_category());
            close(s);
            s = -1;
        }
    }

    if (s == -1) {
        std::ostringstream ss;
        ss << cause << ": " << ec.message();
        throw RpcError(ss.str());
    }

    if (addr->socktype == SOCK_STREAM)
        return std::make_shared<StreamChannel>(s);
    else
        return std::make_shared<DatagramChannel>(s);
}

std::shared_ptr<Channel> connectChannel(
    const std::string& host, uint32_t prog, uint32_t vers,
    const std::string& netid)
{
    auto rpcbind = RpcBind(connectChannel(host, "sunrpc", netid));
    auto uaddr = rpcbind.getaddr(rpcb{prog, vers, "", "", ""});
    if (uaddr == "") {
        throw RpcError("Program not registered");
    }
    return connectChannel(uaddr2taddr(uaddr, netid));
}

std::shared_ptr<Channel> connectChannel(
    const std::string& host, const std::string& service,
    const std::string& netid)
{
    int sock;
    sock = connectSocket(host, service, netid);
    if (std::get<1>(getNetId(netid)) == SOCK_STREAM)
        return std::make_shared<StreamChannel>(sock);
    else
        return std::make_shared<DatagramChannel>(sock);
}

}
