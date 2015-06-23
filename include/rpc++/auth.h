// -*- c++ -*-

#pragma once

#include <rpc++/rpcproto.h>

namespace oncrpc {

class Channel;
class Client;
class opaque_auth;

class Auth
{
public:
    virtual ~Auth() {}

    /// Initialise authentication credentials for communicating using
    /// the given client and channel
    virtual void init(Client* client, Channel* channel);

    /// Encode an RPC call including cred and verf
    virtual uint32_t encode(
	uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc,
	XdrSink* xdrs);

    /// Validate the authentication verifier in a reply
    virtual bool validate(uint32_t seq, opaque_auth& verf) = 0;

    /// Refresh credentials after an authentication error
    virtual bool refresh(auth_stat stat);

private:
    opaque_auth auth_;
};

class AuthNone: public Auth
{
    // Auth overrides
    uint32_t encode(
	uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc,
	XdrSink* xdrs) override;
    bool validate(uint32_t seq, opaque_auth& verf) override;
};

struct authsys_parms
{
    uint32_t stamp;
    std::string machinename;
    uint32_t uid;
    uint32_t gid;
    std::vector<uint32_t> gids;
};

template <typename XDR>
void xdr(authsys_parms& v, XDR* xdrs)
{
    xdr(v.stamp, xdrs);
    xdr(v.machinename, xdrs);
    xdr(v.uid, xdrs);
    xdr(v.gid, xdrs);
    xdr(v.gids, xdrs);
}

class AuthSys: public Auth
{
    AuthSys();

    // Auth overrides
    uint32_t encode(
	uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc,
	XdrSink* xdrs) override;
    bool validate(uint32_t seq, opaque_auth& verf) override;

private:
    std::vector<uint8_t> parms_;
};

}
