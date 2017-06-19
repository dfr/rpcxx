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

// -*- c++ -*-

#pragma once

#include <rpc++/channel.h>
#include <rpc++/xdr.h>

namespace oncrpc {

constexpr unsigned PMAPPROG = 100000;
constexpr unsigned PMAPVERS = 2;
constexpr unsigned PMAPPROC_NULL = 0;
constexpr unsigned PMAPPROC_SET = 1;
constexpr unsigned PMAPPROC_UNSET = 2;
constexpr unsigned PMAPPROC_GETPORT = 3;
constexpr unsigned PMAPPROC_DUMP = 4;
constexpr unsigned PMAPPROC_CALLIT = 4;

struct mapping
{
    uint32_t prog;
    uint32_t vers;
    uint32_t prot;
    uint32_t port;
};

template <typename XDR>
void xdr(RefType<mapping, XDR> v, XDR* xdrs)
{
    xdr(v.prog, xdrs);
    xdr(v.vers, xdrs);
    xdr(v.prot, xdrs);
    xdr(v.port, xdrs);
}

struct pmaplist
{
    mapping map;
    std::unique_ptr<pmaplist> next;
};

template <typename XDR>
void xdr(RefType<pmaplist, XDR> v, XDR* xdrs)
{
    xdr(v.map, xdrs);
    xdr(v.next, xdrs);
}

struct call_args
{
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    std::vector<uint8_t> args;
};

template <typename XDR>
void xdr(RefType<call_args, XDR> v, XDR* xdrs)
{
    xdr(v.prog, xdrs);
    xdr(v.vers, xdrs);
    xdr(v.proc, xdrs);
    xdr(v.args, xdrs);
}

struct call_result
{
    uint32_t port;
    std::vector<uint8_t> res;
};

template <typename XDR>
void xdr(RefType<call_result, XDR> v, XDR* xdrs)
{
    xdr(v.port, xdrs);
    xdr(v.res, xdrs);
}

class Portmap
{
public:
    Portmap(std::shared_ptr<Channel> channel)
        : channel_(channel),
          client_(std::make_shared<Client>(PMAPPROG, PMAPVERS))
    {
    }

    void null()
    {
        channel_->call(client_.get(), PMAPPROC_NULL,
                       [](XdrSink*) {}, [](XdrSource*) {});
    }

    bool set(const mapping& args)
    {
        bool res;
        channel_->call(client_.get(), PMAPPROC_SET,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    bool unset(const mapping& args)
    {
        bool res;
        channel_->call(client_.get(), PMAPPROC_UNSET,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    uint32_t getport(const mapping& args)
    {
        uint32_t res;
        channel_->call(client_.get(), PMAPPROC_GETPORT,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    std::unique_ptr<pmaplist> dump()
    {
        std::unique_ptr<pmaplist> res;
        channel_->call(client_.get(), PMAPPROC_DUMP,
                      [&](XdrSink* xdrs) { },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return std::move(res);
    }

    call_result callit(call_args args)
    {
        call_result res;
        channel_->call(client_.get(), PMAPPROC_CALLIT,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return std::move(res);
    }

private:
    std::shared_ptr<Channel> channel_;
    std::shared_ptr<Client> client_;
};

}
