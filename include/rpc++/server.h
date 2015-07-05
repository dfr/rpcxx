// -*- c++ -*-

#pragma once

#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rpc++/channel.h>

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

class CallContext
{
public:
    CallContext(
        rpc_msg&& msg, std::unique_ptr<XdrMemory> args,
        std::shared_ptr<Channel> chan)
        : msg_(std::move(msg)),
          args_(std::move(args)),
          chan_(chan)
    {
    }

    ~CallContext()
    {
        if (args_)
            chan_->releaseBuffer(std::move(args_));
    }

    void setService(Service svc) { svc_ = svc; }

    const rpc_msg& msg() const { return msg_; }
    uint32_t prog() const { return msg_.cbody().prog; }
    uint32_t vers() const { return msg_.cbody().vers; }
    uint32_t proc() const { return msg_.cbody().proc; }

    void run()
    {
        try {
            svc_(std::move(*this));
        }
        catch (XdrError& e) {
            garbageArgs();
        }
    }

    template <typename F>
    void getArgs(F&& fn)
    {
        fn(static_cast<XdrSource*>(args_.get()));
        chan_->releaseBuffer(std::move(args_));
    }

    template <typename F>
    void sendReply(F&& fn)
    {
        accepted_reply areply;
        areply.verf = { AUTH_NONE, {} };
        areply.stat = SUCCESS;
        rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
        auto reply = chan_->acquireBuffer();
        xdr(reply_msg, static_cast<XdrSink*>(reply.get()));
        fn(reply.get());
        chan_->sendMessage(std::move(reply));
    }

    void rpcMismatch()
    {
        rejected_reply rreply;
        rreply.stat = RPC_MISMATCH;
        rreply.rpc_mismatch.low = 2;
        rreply.rpc_mismatch.high = 2;
        rpc_msg reply_msg(msg_.xid, std::move(rreply));
        auto reply = chan_->acquireBuffer();
        xdr(reply_msg, static_cast<XdrSink*>(reply.get()));
        chan_->sendMessage(std::move(reply));
    }

    void garbageArgs()
    {
        accepted_reply areply;
        areply.verf = { AUTH_NONE, {} };
        areply.stat = GARBAGE_ARGS;
        rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
        auto reply = chan_->acquireBuffer();
        xdr(reply_msg, static_cast<XdrSink*>(reply.get()));
        chan_->sendMessage(std::move(reply));
    }

    void procedureUnavailable()
    {
        accepted_reply areply;
        areply.verf = { AUTH_NONE, {} };
        areply.stat = PROC_UNAVAIL;
        rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
        auto reply = chan_->acquireBuffer();
        xdr(reply_msg, static_cast<XdrSink*>(reply.get()));
        chan_->sendMessage(std::move(reply));
    }

    void programUnavailable()
    {
        accepted_reply areply;
        areply.verf = { AUTH_NONE, {} };
        areply.stat = PROG_UNAVAIL;
        rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
        auto reply = chan_->acquireBuffer();
        xdr(reply_msg, static_cast<XdrSink*>(reply.get()));
        chan_->sendMessage(std::move(reply));
    }

    void versionMismatch(int low, int high)
    {
        accepted_reply areply;
        areply.verf = { AUTH_NONE, {} };
        areply.stat = PROG_MISMATCH;
        auto& mi = areply.mismatch_info;
        mi.low = low;
        mi.high = high;
        rpc_msg reply_msg(msg_.xid, reply_body(std::move(areply)));
        auto reply = chan_->acquireBuffer();
        xdr(reply_msg, static_cast<XdrSink*>(reply.get()));
        chan_->sendMessage(std::move(reply));
    }

private:
    /// RPC Message for this call
    rpc_msg msg_;

    /// Xdr encoded arguments for the call
    std::unique_ptr<XdrMemory> args_;

    /// Channel to send reply (if any)
    std::shared_ptr<Channel> chan_;

    /// Service handler
    Service svc_;
};

class ServiceRegistry
{
public:
    void add(uint32_t prog, uint32_t vers, Service&& svc);

    void remove(uint32_t prog, uint32_t vers);

    const Service lookup(uint32_t prog, uint32_t vers) const;

    // Process an RPC message and possibly dispatch to a suitable handler
    void process(CallContext&& ctx);

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> programs_;
    std::unordered_map<std::pair<uint32_t, uint32_t>, Service> services_;
};

}
