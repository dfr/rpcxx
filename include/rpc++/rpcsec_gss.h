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
