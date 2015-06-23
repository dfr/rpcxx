// -*- c++ -*-

#pragma once

#include <rpc++/auth.h>

namespace oncrpc {

/// Common state for all calls to a given service on some channel
class Client
{
public:
    Client(uint32_t program, uint32_t version,
	   std::unique_ptr<Auth> auth = nullptr);

    uint32_t program() const { return program_; }
    uint32_t version() const { return version_; }
    
    void validateAuth(Channel* chan);
    uint32_t processCall(
	Channel* chan, uint32_t xid, uint32_t proc,
	XdrSink* xdrs, std::function<void(XdrSink*)> xargs);
    void processReply(
	uint32_t seq,
	accepted_reply& areply,
	XdrSource* xdrs, std::function<void(XdrSource*)> xresults);
    bool authError(auth_stat stat);

private:
    uint32_t program_;
    uint32_t version_;
    std::unique_ptr<Auth> auth_;
};


}
