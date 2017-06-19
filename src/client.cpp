/*-
 * Copyright (c) 2016-present Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <unistd.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/cred.h>

using namespace oncrpc;

Client::Client(uint32_t program, uint32_t version)
    : program_(program),
      version_(version)
{
}

Client::~Client()
{
}

int
Client::validateAuth(Channel* chan, bool revalidate)
{
    return 1;
}

bool
Client::processCall(
    uint32_t xid, int gen, uint32_t proc, XdrSink* xdrs,
    std::function<void(XdrSink*)> xargs, Protection prot,
    uint32_t& seq)
{
    if (prot != Protection::DEFAULT && prot != Protection::NONE) {
        throw RpcError("unsupported protection");
    }
    encodeCall(xid, proc, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xargs(xdrs);
    seq = 0;
    return true;
}

bool
Client::processReply(
    uint32_t seq, int gen, accepted_reply& areply,
    XdrSource* xdrs, std::function<void(XdrSource*)> xresults, Protection prot)
{
    if (prot != Protection::DEFAULT && prot != Protection::NONE) {
        throw RpcError("unsupported protection");
    }
    xresults(xdrs);
    return true;
}

bool
Client::authError(int gen, int stat)
{
    return false;
}

void
Client::encodeCall(uint32_t xid, uint32_t proc, XdrSink* xdrs)
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
    int32_t uid;
    int32_t gid;
    std::vector<int32_t> gids;
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
    machinename_ = hostname;

    Credential cred;
    cred.setToLocal();
    set(cred);
}

bool
SysClient::processCall(
    uint32_t xid, int gen, uint32_t proc, XdrSink* xdrs,
    std::function<void(XdrSink*)> xargs, Protection prot,
    uint32_t& seq)
{
    if (prot != Protection::DEFAULT && prot != Protection::NONE) {
        throw RpcError("unsupported protection");
    }
    encodeCall(xid, proc, xdrs);
    xdrs->putWord(AUTH_SYS);
    xdr(cred_, xdrs);
    xdrs->putWord(AUTH_NONE);
    xdrs->putWord(0);
    xargs(xdrs);
    seq = 0;
    return true;
}

void
SysClient::set(const Credential& cred)
{
    authsys_parms parms;
    parms.stamp = 0;
    parms.machinename = machinename_;
    parms.uid = cred.uid();
    parms.gid = cred.gid();
    parms.gids = cred.gids();

    cred_.resize(XdrSizeof(parms));
    auto xdrs = std::make_unique<XdrMemory>(cred_.data(), cred_.size());
    xdr(parms, static_cast<XdrSink*>(xdrs.get()));
    assert(xdrs->writePos() == cred_.size());
}
