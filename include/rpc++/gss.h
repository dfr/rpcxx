// -*- c++ -*-

#pragma once

#ifdef __APPLE__
#include <GSS/GSS.h>
#else
#include <gssapi/gssapi.h>
#endif

#include <rpc++/auth.h>
#include <rpc++/rpcsec_gss.h>

namespace oncrpc {

/// Client-side authentication mechanism which implements RPCSEC_GSS
/// version 1.
class GssAuth: public Auth
{
public:
    GssAuth(
	const std::string& principal,
	const std::string& mechanism,
	rpc_gss_service_t service);

    void init(Client* client, Channel* channel) override;
    uint32_t encode(
	uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc,
	XdrSink* xdrs) override;
    bool validate(uint32_t seq, opaque_auth& verf) override;
    bool refresh(auth_stat stat) override;

private:
    gss_OID_desc* mech_;	  // GSS-API mechanism
    gss_ctx_id_t context_;	  // GSS-API context
    gss_cred_id_t cred_;	  // GSS-API credential
    gss_name_t principal_;	  // GSS-API name for remote principal
    uint32_t seq_window_;	  // size of the sequence window
    rpc_gss_cred_vers_1_t state_; // wire-level protocol state
    std::vector<uint8_t> verf_;	  // used to verify seq_window_
};

}
