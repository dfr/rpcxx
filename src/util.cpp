#include <sstream>

#include <unistd.h>

#include <rpc++/util.h>

namespace oncrpc {

std::vector<AddressInfo> getAddressInfo(
    const std::string& host, const std::string& service, int socktype)
{
    addrinfo hints;
    addrinfo* res0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = socktype;
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
    const std::string& host, const std::string& service, int socktype)
{
    auto addrs = getAddressInfo(host, service, socktype);

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

}
