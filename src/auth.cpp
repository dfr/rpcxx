#include <unistd.h>

#include <rpc++/auth.h>

using namespace oncrpc;

void
Auth::init(Client* client, Channel* channel)
{
}

uint32_t
Auth::encode(
    uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc, XdrSink* xdrs)
{
    // Derived classes should call this before encoding cred and verf
    xdrs->putWord(xid);
    xdrs->putWord(CALL);
    xdrs->putWord(2);
    xdrs->putWord(prog);
    xdrs->putWord(vers);
    xdrs->putWord(proc);
    return 0;
}

bool
Auth::refresh(auth_stat stat)
{
    return false;
}

uint32_t
AuthNone::encode(
    uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc, XdrSink* xdrs)
{
    Auth::encode(xid, prog, vers, proc, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    return 0;
}

bool
AuthNone::validate(uint32_t seq, opaque_auth& verf)
{
    return true;
}

AuthSys::AuthSys()
{
    char hostname[255];
    gethostname(hostname, 254);
    hostname[254] = '\0';

    std::vector<gid_t> gids;
    gids.resize(getgroups(0, nullptr));
    getgroups(gids.size(), gids.data());

    authsys_parms parms;
    parms.stamp = 0;
    parms.machinename = hostname;
    parms.uid = getuid();
    parms.gid = getgid();
    parms.gids.resize(gids.size());
    std::copy(gids.begin(), gids.end(), parms.gids.begin());

    parms_.resize(512);
    auto xdrs = std::make_unique<XdrMemory>(parms_.data(), 512);
    xdr(parms, static_cast<XdrSink*>(xdrs.get()));
    parms_.resize(xdrs->pos());
}

uint32_t
AuthSys::encode(
    uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc, XdrSink* xdrs)
{
    Auth::encode(xid, prog, vers, proc, xdrs);
    xdrs->putWord(AUTH_SYS);
    xdr(parms_, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    return 0;
}

bool
AuthSys::validate(uint32_t seq, opaque_auth& verf)
{
    // We could implement AUTH_SHORT here but there isn't much
    // point since it won't really affect performance and many
    // server implementations don't bother with it.
    return true;
}
