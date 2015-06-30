// -*- c++ -*-

#pragma once

#include <string>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>

namespace oncrpc {

class Channel;

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

std::unique_ptr<AddressInfo> uaddr2taddr(
    const std::string& uaddr, const std::string& nettype);

std::vector<AddressInfo> getAddressInfo(
    const std::string& host, const std::string& service, 
    const std::string& nettype);

int connectSocket(
    const std::string& host, const std::string& service,
    const std::string& nettype);

std::shared_ptr<Channel> connectChannel(std::unique_ptr<AddressInfo>&& addr);

std::shared_ptr<Channel> connectChannel(
    const std::string& host, uint32_t prog, uint32_t vers,
    const std::string& nettype);

std::shared_ptr<Channel> connectChannel(
    const std::string& host, const std::string& service,
    const std::string& nettype);

}

