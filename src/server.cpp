#include <cassert>
#include <system_error>

#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <rpc++/rpcproto.h>
#include <rpc++/server.h>

using namespace oncrpc;

void
ServiceRegistry::add(
        uint32_t prog, uint32_t vers, ServiceEntry&& entry)
{
    programs_[prog].insert(vers);
    services_[std::make_pair(prog, vers)] = entry;
}

void
ServiceRegistry::remove(uint32_t prog, uint32_t vers)
{
    auto p = programs_.find(prog);
    assert(p != programs_.end());
    p->second.erase(vers);
    if (p->second.size() == 0)
        programs_.erase(prog);
    services_.erase(std::pair<uint32_t, uint32_t>(prog, vers));
}

const ServiceEntry*
ServiceRegistry::lookup(uint32_t prog, uint32_t vers) const
{
    auto p = services_.find(std::make_pair(prog, vers));
    if (p == services_.end())
        return nullptr;
    return &p->second;
}

bool
ServiceRegistry::process(XdrSource* xdrin, XdrSink* xdrout)
{
    rpc_msg msg;
    try {
        xdr(msg, xdrin);
    }
    catch (XdrError& e) {
        return false;
    }
    return process(std::move(msg), xdrin, xdrout);
}

bool
ServiceRegistry::process(rpc_msg&& msg, XdrSource* xdrin, XdrSink* xdrout)
{
    rpc_msg call_msg = std::move(msg);

    if (call_msg.mtype != CALL)
        return false;

    if (call_msg.cbody().rpcvers != 2) {
        rejected_reply rreply;
        rreply.stat = RPC_MISMATCH;
        rreply.rpc_mismatch.low = 2;
        rreply.rpc_mismatch.high = 2;
        rpc_msg reply_msg(call_msg.xid, std::move(rreply));
        xdr(reply_msg, xdrout);
        return true;
    }

    // XXX validate auth

    accepted_reply areply;

    areply.verf = { AUTH_NONE, {} };
    auto p = services_.find(
        std::make_pair(call_msg.cbody().prog, call_msg.cbody().vers));
    if (p != services_.end()) {
        auto proc = call_msg.cbody().proc;
        const auto& entry = p->second;
        if (entry.procs.find(proc) != entry.procs.end()) {
            areply.stat = SUCCESS;
            rpc_msg reply_msg(call_msg.xid, reply_body(std::move(areply)));
            xdr(reply_msg, xdrout);
            entry.handler(proc, xdrin, xdrout);
        }
        else {
            areply.stat = PROC_UNAVAIL;
            rpc_msg reply_msg(call_msg.xid, reply_body(std::move(areply)));
            xdr(reply_msg, xdrout);
        }
    }
    else {
        // Figure out which error message to use
        auto p = programs_.find(call_msg.cbody().prog);
        if (p == programs_.end()) {
            areply.stat = PROG_UNAVAIL;
        }
        else {
            areply.stat = PROG_MISMATCH;
            auto& mi = areply.mismatch_info;
            mi.low = ~0U;
            mi.high = 0;
            const auto& entry = p->second;
            for (const auto& vers: entry) {
                mi.low = std::min(mi.low, vers);
                mi.high = std::max(mi.high, vers);
            }
        }
        rpc_msg reply_msg(call_msg.xid, reply_body(std::move(areply)));
        xdr(reply_msg, xdrout);
    }
    return true;
}
