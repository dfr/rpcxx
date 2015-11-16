// -*- c++ -*-

#pragma once

#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rpc++/channel.h>
#include <rpc++/errors.h>
#include <rpc++/gss.h>

namespace std {

template <>
class hash<std::pair<uint32_t, uint32_t>>
{
public:
    size_t operator()(const std::pair<uint32_t, uint32_t>& v) const
    {
        std::hash<uint32_t> h;
        return h(v.first) ^ h(v.second);
    }
};

}

namespace oncrpc {

class CallContext;

typedef std::function<void(CallContext&&)> Service;

namespace _detail {

class SequenceWindow
{
public:
    SequenceWindow(int size);

    int size() const { return size_; }
    void update(uint32_t seq);
    void reset(uint32_t seq);
    bool valid(uint32_t seq);

private:
    int size_;
    uint32_t largestSeen_;
    std::deque<uint32_t> valid_;
};

class GssClientContext
{
public:
    GssClientContext();
    ~GssClientContext();

    void controlMessage(CallContext& ctx);

    bool verifyCall(CallContext& ctx);

    template <typename F>
    void getArgs(F&& fn, GssCred& cred, XdrSource* xdrs)
    {
        if (cred.proc == GssProc::DATA) {
            std::unique_lock<std::mutex> lock(mutex_);
            decodeBody(
                context_, mechType_, cred.service, cred.sequence, fn, xdrs);
        }
        else {
            fn(xdrs);
        }
    }

    template <typename F>
    bool sendReply(F&& fn, GssCred& cred, XdrSink* xdrs)
    {
        if (cred.proc == GssProc::DATA) {
            std::unique_lock<std::mutex> lock(mutex_);
            try {
                encodeBody(
                    context_, mechType_, cred.service, cred.sequence, fn, xdrs);
            }
            catch (RpcError& e) {
                return false;
            }
        }
        else {
            fn(xdrs);
        }
        return true;
    }

    bool getVerifier(CallContext& ctx, opaque_auth& verf);

    uint32_t id() const { return id_; }

    auto expiry() const { return expiry_; }

    void setExpiry(std::chrono::system_clock::time_point expiry)
    {
        expiry_ = expiry;
    }

private:
    static uint32_t nextId_;    // XXX atomic?

    uint32_t id_;
    std::mutex mutex_;
    bool established_ = false;
    std::chrono::system_clock::time_point expiry_;
    SequenceWindow sequenceWindow_;
    gss_ctx_id_t context_ = GSS_C_NO_CONTEXT;
    gss_name_t clientName_ = GSS_C_NO_NAME;
    gss_OID mechType_ = GSS_C_NO_OID;
};

}

class CallContext
{
public:
    CallContext(
        rpc_msg&& msg, std::unique_ptr<XdrSource> args,
        std::shared_ptr<Channel> chan);

    CallContext(CallContext&& other);

    ~CallContext();

    size_t size() const { return size_; }

    void setService(Service svc)
    {
        svc_ = svc;
    }

    void setClient(std::shared_ptr<_detail::GssClientContext> client)
    {
        client_ = client;
    }

    const rpc_msg& msg() const { return msg_; }
    uint32_t prog() const { return msg_.cbody().prog; }
    uint32_t vers() const { return msg_.cbody().vers; }
    uint32_t proc() const { return msg_.cbody().proc; }
    GssCred& gsscred() { return gsscred_; }

    /// Call the service method, sending replies as necessary
    void operator()();

    /// Parse procedure arguments using the supplied function
    void getArgs(std::function<void(XdrSource*)> fn);

    /// Send a reply message with results encoded using the supplied function
    void sendReply(std::function<void(XdrSink*)> fn);

    /// Send an RPC_MISMATCH reply
    void rpcMismatch();

    /// Send a GARBAGE_ARGS reply
    void garbageArgs();

    /// Send a PROC_UNAVAIL reply
    void procedureUnavailable();

    /// Send a PROG_UNAVAIL reply
    void programUnavailable();

    /// Send a PROG_MISMATCH reply
    void versionMismatch(int low, int high);

    /// Send an AUTH_ERROR reply
    void authError(auth_stat stat);

private:
    bool getVerifier(opaque_auth& verf);

    /// Size in bytes of the wire format message
    size_t size_;

    /// RPC Message for this call
    rpc_msg msg_;

    /// Decoded RPCSEC_GSS credentials
    GssCred gsscred_;

    /// Xdr encoded arguments for the call
    std::unique_ptr<XdrSource> args_;

    /// Channel to send reply (if any)
    std::shared_ptr<Channel> chan_;

    /// Service handler
    Service svc_;

    /// RPCSEC_GSS client context
    std::shared_ptr<_detail::GssClientContext> client_;
};

class ServiceRegistry
{
public:
    ServiceRegistry();

    /// Add a handler to the registry
    void add(uint32_t prog, uint32_t vers, Service&& svc);

    /// Remove the handler for the given program and version
    void remove(uint32_t prog, uint32_t vers);

    /// Look up a service handler for the given program and version
    const Service lookup(uint32_t prog, uint32_t vers) const;

    /// Process an RPC message and possibly dispatch to a suitable handler
    void process(CallContext&& ctx);

    /// Used in unit tests to force RPCSEC_GSS to re-initialise its context
    void clearClients();

    /// Used in unit tests to force client expiry
    void setClientLifetime(std::chrono::system_clock::duration lifetime)
    {
        clientLifetime_ = lifetime;
    }

private:
    bool validateAuth(CallContext& ctx);

    mutable std::mutex mutex_;
    std::chrono::system_clock::duration clientLifetime_;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> programs_;
    std::unordered_map<std::pair<uint32_t, uint32_t>, Service> services_;
    std::unordered_map<
        uint32_t, std::shared_ptr<_detail::GssClientContext>> clients_;
};

}
