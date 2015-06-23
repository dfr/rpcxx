// -*- c++ -*-

#pragma once

#include <rpc++/xdr.h>

namespace oncrpc {

/// RPCSEC_GSS control procedures
enum rpc_gss_proc_t: uint32_t {
    RPCSEC_GSS_DATA = 0,
    RPCSEC_GSS_INIT = 1,
    RPCSEC_GSS_CONTINUE_INIT = 2,
    RPCSEC_GSS_DESTROY = 2
};

/// RPCSEC_GSS services
enum rpc_gss_service_t: uint32_t {
    /* Note: the enumerated value for 0 is reserved. */
    RPCSEC_GSS_SVC_NONE = 1,
    RPCSEC_GSS_SVC_INTEGRITY = 2,
    RPCSEC_GSS_SVC_PRIVACY = 3
};

constexpr uint32_t MAXSEQ = 0x80000000;

struct rpc_gss_cred_vers_1_t
{
    rpc_gss_proc_t gss_proc;	// control procedure
    uint32_t seq_num;		// sequence number
    rpc_gss_service_t service;	// service used
    std::vector<uint8_t> handle; // context handle
};

template <typename XDR>
void xdr(rpc_gss_cred_vers_1_t& v, XDR* xdrs)
{
    xdr(reinterpret_cast<uint32_t&>(v.gss_proc), xdrs);
    xdr(v.seq_num, xdrs);
    xdr(reinterpret_cast<uint32_t&>(v.service), xdrs);
    xdr(v.handle, xdrs);
}

struct rpc_gss_init_res {
    std::vector<uint8_t> handle;
    uint32_t gss_major;
    uint32_t gss_minor;
    uint32_t seq_window;
    std::vector<uint8_t> gss_token;
};

template <typename XDR>
void xdr(rpc_gss_init_res& v, XDR* xdrs)
{
    xdr(v.handle, xdrs);
    xdr(v.gss_major, xdrs);
    xdr(v.gss_minor, xdrs);
    xdr(v.seq_window, xdrs);
    xdr(v.gss_token, xdrs);
}

}
