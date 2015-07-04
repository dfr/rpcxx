// -*- c++ -*-

#pragma once

#include <string>

#include <rpc++/socket.h>

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

std::unique_ptr<AddressInfo> uaddr2taddr(
    const std::string& uaddr, const std::string& nettype);

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
