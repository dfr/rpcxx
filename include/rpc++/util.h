// -*- c++ -*-

#pragma once

#include <string>

#include <rpc++/rpcproto.h>
#include <rpc++/socket.h>

namespace oncrpc {

class Channel;

std::unique_ptr<AddressInfo> uaddr2taddr(
    const std::string& uaddr, const std::string& netid);

int connectSocket(
    const std::string& host, const std::string& service,
    const std::string& netid);

std::shared_ptr<Channel> connectChannel(std::unique_ptr<AddressInfo>&& addr);

std::shared_ptr<Channel> connectChannel(
    const std::string& host, uint32_t prog, uint32_t vers,
    const std::string& netid);

std::shared_ptr<Channel> connectChannel(
    const std::string& host, const std::string& service,
    const std::string& netid);

}
