#include <cassert>
#include <system_error>

#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <rpc++/errors.h>
#include <rpc++/rpcproto.h>
#include <rpc++/server.h>

using namespace oncrpc;

void
ServiceRegistry::add(uint32_t prog, uint32_t vers, Service&& svc)
{
    std::unique_lock<std::mutex> lock(mutex_);
    programs_[prog].insert(vers);
    services_[std::make_pair(prog, vers)] = std::move(svc);
}

void
ServiceRegistry::remove(uint32_t prog, uint32_t vers)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto p = programs_.find(prog);
    assert(p != programs_.end());
    p->second.erase(vers);
    if (p->second.size() == 0)
        programs_.erase(prog);
    services_.erase(std::pair<uint32_t, uint32_t>(prog, vers));
}

const Service
ServiceRegistry::lookup(uint32_t prog, uint32_t vers) const
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto p = services_.find(std::make_pair(prog, vers));
    if (p == services_.end())
        throw ProgramUnavailable(prog);
    return p->second;
}

void
ServiceRegistry::process(CallContext&& ctx)
{
    const rpc_msg& call_msg = ctx.msg();

    if (call_msg.mtype != CALL)
        return;

    if (call_msg.cbody().rpcvers != 2) {
        ctx.rpcMismatch();
        return;
    }

    // XXX validate auth

    try {
        // Simple single-threaded dispatch. To implement more sophisticated
        // dispatch mechanisms, we could wrap the value returned by lookup
        // with a shim which moves the call context to be executed by a thread
        // pool. Alternatively, the application can supply a service handler
        // which could defer execution to some other executor.
        ctx.setService(lookup(ctx.prog(), ctx.vers()));
        ctx.run();
    }
    catch (ProgramUnavailable& e) {
        // Figure out which error message to use
        std::unique_lock<std::mutex> lock(mutex_);
        auto p = programs_.find(ctx.prog());
        if (p == programs_.end()) {
            lock.unlock();
            ctx.programUnavailable();
        }
        else {
            uint32_t low = ~0U;
            uint32_t high = 0;
            const auto& entry = p->second;
            for (const auto& vers: entry) {
                low = std::min(low, vers);
                high = std::max(high, vers);
            }
            lock.unlock();
            ctx.versionMismatch(low, high);
        }
    }
}
