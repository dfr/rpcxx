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

#include <rpc++/xdr.h>

namespace oncrpc {

/// RPCSEC_GSS control procedures
enum class GssProc: uint32_t {
    DATA = 0,
    INIT = 1,
    CONTINUE_INIT = 2,
    DESTROY = 2
};

/// RPCSEC_GSS services
enum class GssService: uint32_t {
    // Note: the enumerated value for 0 is reserved.
    NONE = 1,
    INTEGRITY = 2,
    PRIVACY = 3
};

constexpr uint32_t RPCSEC_GSS_MAXSEQ = 0x80000000;

/// This cred structure covers RPCSEC_GSS versions 1 and 2. We only support
/// version 1.
struct GssCred
{
    uint32_t version = 1;
    GssProc proc;                   // control procedure
    uint32_t sequence;              // sequence number
    GssService service;             // service used
    std::vector<uint8_t> handle;    // context handle
};

template <typename XDR>
void xdr(RefType<GssCred, XDR> v, XDR* xdrs)
{
    xdr(v.version, xdrs);
    xdr(reinterpret_cast<RefType<uint32_t, XDR>>(v.proc), xdrs);
    xdr(v.sequence, xdrs);
    xdr(reinterpret_cast<RefType<uint32_t, XDR>>(v.service), xdrs);
    xdr(v.handle, xdrs);
}

struct GssInitResult {
    std::vector<uint8_t> handle;    // server side client handle
    uint32_t major;                 // GSS-API major status
    uint32_t minor;                 // GSS-API minor status
    uint32_t sequenceWindow;        // size of the sequence window
    std::vector<uint8_t> token;     // reply token form gss_accept_sec_context
};

template <typename XDR>
void xdr(RefType<GssInitResult, XDR> v, XDR* xdrs)
{
    xdr(v.handle, xdrs);
    xdr(v.major, xdrs);
    xdr(v.minor, xdrs);
    xdr(v.sequenceWindow, xdrs);
    xdr(v.token, xdrs);
}

}
