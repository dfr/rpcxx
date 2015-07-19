// -*- c++ -*-

#pragma once

#include <functional>
#include <vector>

namespace oncrpc {

class Channel;
class XdrSink;
class XdrSource;
class accepted_reply;

/// Quality of protection used for RPC messages on a channel. Note: these
/// values are chosen to match those used in the RPCSEC_GSS protocol
enum class Protection
{
    DEFAULT = 0,        // The default value from the Client
    NONE = 1,           // no protection
    INTEGRITY = 2,      // modifications to the RPC body are detectable
    PRIVACY = 3         // RPC body contents are encrypted
};

/// An RPC client which makes calls on some channel using AUTH_NONE
/// authentication
class Client
{
public:
    Client(uint32_t program, uint32_t version);
    virtual ~Client();

    /// Return the RPC program to call
    uint32_t program() const { return program_; }

    /// Return the RPC program version number
    uint32_t version() const { return version_; }

    /// Validate the client, re-initialising auth state as necessary and
    /// return a generation number which can be used to detect when the auth
    /// state has changed
    virtual int validateAuth(Channel* chan);

    /// Encode a call message including cred, verf and message body.
    /// For RPCSEC_GSS, the value returned in seq can be used to validate
    /// the corresponding reply
    virtual bool processCall(
        uint32_t xid, uint32_t& seq, uint32_t proc, XdrSink* xdrs,
        std::function<void(XdrSink*)> xargs, Protection prot);

    /// Validate a reply and decode results. Return true if the reply is valid.
    virtual bool processReply(
        uint32_t seq, int gen, accepted_reply& areply,
        XdrSource* xdrs, std::function<void(XdrSource*)> xresults,
        Protection prot);

    /// Handle an AUTH_ERROR reply. Return true if the call should be re-tried
    virtual bool authError(int gen, int stat);

protected:
    /// Encode the call header, not including cred and verf.
    void encodeCall(uint32_t xid, uint32_t proc, XdrSink* xdrs);

    uint32_t program_;
    uint32_t version_;
};

/// An RPC client using AUTH_SYS authentication
class SysClient: public Client
{
public:
    SysClient(uint32_t program, uint32_t version);

    bool processCall(
        uint32_t xid, uint32_t& seq, uint32_t proc, XdrSink* xdrs,
        std::function<void(XdrSink*)> xargs, Protection prot) override;

private:
    std::vector<uint8_t> cred_;
};

}
