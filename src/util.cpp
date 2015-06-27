#include <sstream>

#include <unistd.h>

#include <rpc++/channel.h>
#include <rpc++/rpcbind.h>
#include <rpc++/util.h>

namespace oncrpc {

static std::pair<int, int> getNetType(const std::string& nettype)
{
    if (nettype == "tcp")
	return std::make_pair(PF_UNSPEC, SOCK_STREAM);
    if (nettype == "udp")
	return std::make_pair(PF_UNSPEC, SOCK_DGRAM);

    if (nettype == "tcp4")
	return std::make_pair(PF_INET, SOCK_STREAM);
    if (nettype == "udp4")
	return std::make_pair(PF_INET, SOCK_DGRAM);

    if (nettype == "tcp6")
	return std::make_pair(PF_INET6, SOCK_STREAM);
    if (nettype == "udp6")
	return std::make_pair(PF_INET6, SOCK_DGRAM);

    throw RpcError("Bad nettype");
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

std::unique_ptr<AddressInfo> uaddr2taddr(
    const std::string& uaddr, const std::string& nettype)
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
    auto nt = getNetType(nettype);
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

std::vector<AddressInfo> getAddressInfo(
    const std::string& host, const std::string& service,
    const std::string& nettype)
{
    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    auto nt = getNetType(nettype);
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

int connectSocket(
    const std::string& host, const std::string& service,
    const std::string& nettype)
{
    auto addrs = getAddressInfo(host, service, nettype);

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
    const std::string& nettype)
{
    auto rpcbind = RpcBind(connectChannel(host, "sunrpc", nettype));
    auto uaddr = rpcbind.getaddr(rpcb{prog, vers, "", "", ""});
    if (uaddr == "") {
	throw RpcError("Program not registered");
    }
    return connectChannel(uaddr2taddr(uaddr, nettype));
}

std::shared_ptr<Channel> connectChannel(
    const std::string& host, const std::string& service,
    const std::string& nettype)
{
    int sock;
    sock = connectSocket(host, service, nettype);
    if (std::get<1>(getNetType(nettype)) == SOCK_STREAM)
	return std::make_shared<StreamChannel>(sock);
    else
	return std::make_shared<DatagramChannel>(sock);
}

}
