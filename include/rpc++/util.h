// -*- c++ -*-

#pragma once

#include <string>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>

namespace oncrpc {

class RpcError: public std::runtime_error
{
public:
    RpcError(const std::string& what)
	: std::runtime_error(what)
    {
    }

    RpcError(const char* what)
	: std::runtime_error(what)
    {
    }
};

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
    const std::string& host, const std::string& service, int socktype);

int connectSocket(
    const std::string& host, const std::string& service, int socktype);

}

