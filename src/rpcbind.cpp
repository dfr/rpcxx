#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <rpc++/rpcbind.h>

namespace oncrpc {

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
    int family, int socktype, const std::string& uaddr)
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
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = family;
    hints.ai_socktype = socktype;
    std::string service = std::to_string(port);

    int err = getaddrinfo(host.c_str(), service.c_str(), &hints, &addrs);
    if (err)
	throw RpcError(std::string("getaddrinfo: ") + gai_strerror(err));

    auto res = std::make_unique<AddressInfo>(addrs);
    freeaddrinfo(addrs);
    return std::move(res);
}

}
