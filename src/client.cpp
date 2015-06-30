#include <unistd.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>

using namespace oncrpc;

Client::Client(uint32_t program, uint32_t version)
    : program_(program),
      version_(version)
{
}

uint32_t
Client::processCall(
    uint32_t xid, uint32_t proc, XdrSink* xdrs,
    std::function<void(XdrSink*)> xargs)
{
    encodeCall(xid, proc, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xargs(xdrs);
    return 0;
}

bool
Client::processReply(
    uint32_t seq, accepted_reply& areply,
    XdrSource* xdrs, std::function<void(XdrSource*)> xresults)
{
    xresults(xdrs);
    return true;
}

bool
Client::authError(auth_stat stat)
{
    assert(false);
}

void
Client::encodeCall(
    uint32_t xid, uint32_t proc, XdrSink* xdrs)
{
    // Derived classes should call this before encoding cred and verf
    XdrWord* p = xdrs->writeInline<XdrWord>(6 * sizeof(XdrWord));
    if (p) {
        *p++ = xid;
        *p++ = CALL;
        *p++ = 2;
        *p++ = program_;
        *p++ = version_;
        *p++ = proc;
    }
    else {
        xdrs->putWord(xid);
        xdrs->putWord(CALL);
        xdrs->putWord(2);
        xdrs->putWord(program_);
        xdrs->putWord(version_);
        xdrs->putWord(proc);
    }
}

namespace {

struct authsys_parms
{
    uint32_t stamp;
    std::string machinename;
    uint32_t uid;
    uint32_t gid;
    std::vector<uint32_t> gids;
};

template <typename XDR>
static void xdr(RefType<authsys_parms, XDR> v, XDR* xdrs)
{
    xdr(v.stamp, xdrs);
    xdr(v.machinename, xdrs);
    xdr(v.uid, xdrs);
    xdr(v.gid, xdrs);
    xdr(v.gids, xdrs);
}

}

SysClient::SysClient(uint32_t program, uint32_t version)
    : Client(program, version)
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

    cred_.resize(512);
    auto xdrs = std::make_unique<XdrMemory>(cred_.data(), 512);
    xdr(parms, static_cast<XdrSink*>(xdrs.get()));
    cred_.resize(xdrs->writePos());
}

uint32_t
SysClient::processCall(
    uint32_t xid, uint32_t proc, XdrSink* xdrs,
    std::function<void(XdrSink*)> xargs)
{
    encodeCall(xid, proc, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdr(cred_, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xargs(xdrs);
    return 0;
}
