// -*- c++ -*-

#pragma once

#include <functional>

#include <rpc++/rpcproto.h>

namespace oncrpc {

class Channel;
class XdrSink;
class XdrSource;

/// An RPC client which makes calls on some channel using AUTH_NONE
/// authentication
class Client
{
public:
    Client(uint32_t program, uint32_t version);

    uint32_t program() const { return program_; }
    uint32_t version() const { return version_; }
    
    virtual void validateAuth(Channel* chan) {}
    virtual uint32_t processCall(
	uint32_t xid, uint32_t proc, XdrSink* xdrs,
	std::function<void(XdrSink*)> xargs);
    virtual bool processReply(
	uint32_t seq,
	accepted_reply& areply,
	XdrSource* xdrs, std::function<void(XdrSource*)> xresults);
    virtual bool authError(auth_stat stat);

protected:
    void encodeCall(
	uint32_t xid, uint32_t proc,
	XdrSink* xdrs);

    uint32_t program_;
    uint32_t version_;
};

/// An RPC client using AUTH_SYS authentication
class SysClient: public Client
{
public:
    SysClient(uint32_t program, uint32_t version);

    uint32_t processCall(
	uint32_t xid, uint32_t proc, XdrSink* xdrs,
	std::function<void(XdrSink*)> xargs) override;

private:
    std::vector<uint8_t> cred_;
};

}
