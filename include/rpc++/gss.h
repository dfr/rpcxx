// -*- c++ -*-

#pragma once

#ifdef __APPLE__
#include <GSS/GSS.h>
#else
#include <gssapi/gssapi.h>
#endif

#include <rpc++/client.h>
#include <rpc++/rpcsec_gss.h>

namespace oncrpc {

/// An RPC client using RPCSEC_GSS version 1 authentication
class GssClient: public Client
{
public:
    GssClient(uint32_t program, uint32_t version,
	      const std::string& principal,
	      const std::string& mechanism,
	      rpc_gss_service_t service);

    void validateAuth(Channel* chan) override;
    uint32_t processCall(
	uint32_t xid, uint32_t proc, XdrSink* xdrs,
	std::function<void(XdrSink*)> xargs) override;
    bool processReply(
	uint32_t seq,
	accepted_reply& areply,
	XdrSource* xdrs, std::function<void(XdrSource*)> xresults) override;
    //bool authError(auth_stat stat) override;

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
