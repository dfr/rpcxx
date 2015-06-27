// -*- c++ -*-

#pragma once

#ifdef __APPLE__
#include <GSS/GSS.h>
#include <CoreFoundation/CoreFoundation.h>
#else
#include <gssapi/gssapi.h>
#endif

#include <glog/logging.h>

#include <rpc++/client.h>
#include <rpc++/rpcsec_gss.h>

namespace oncrpc {

namespace _gssdetail {

[[noreturn]] void reportError(gss_OID mech, uint32_t maj, uint32_t min);

/// Decode a message body given the RPCSEC_GSS service and sequence
/// number
template <typename F>
bool decodeBody(
    gss_ctx_id_t context, gss_OID mech,
    rpc_gss_service_t service, uint32_t seq,
    F&& xbody, XdrSource* xdrs)
{
    switch (service) {
    case rpcsec_gss_svc_none:
	xbody(xdrs);
	break;

    case rpcsec_gss_svc_integrity: {
	std::vector<uint8_t> body;
	std::vector<uint8_t> checksum;
	xdr(body, xdrs);
	xdr(checksum, xdrs);

	gss_buffer_desc buf { body.size(), body.data() };
	gss_buffer_desc mic { checksum.size(), checksum.data() };
	uint32_t maj_stat, min_stat;
	maj_stat = gss_verify_mic(
	    &min_stat, context, &buf, &mic, nullptr);
	if (GSS_ERROR(maj_stat)) {
	    if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
		// XXX destroy context and re-init
	    }
	    reportError(mech, maj_stat, min_stat);
	}

	XdrMemory xm(body.data(), body.size());
	uint32_t checkSeq;
	xm.getWord(checkSeq);
	xbody(&xm);

	if (checkSeq != seq) {
	    VLOG(1) << "Bad sequence number in reply:"
		    << " expected " << seq
		    << " received " << checkSeq;
	    return false;
	}
	break;
    }

    case rpcsec_gss_svc_privacy: {
	std::vector<uint8_t> wrappedBody;
	xdr(wrappedBody, xdrs);

	gss_buffer_desc buf { wrappedBody.size(), wrappedBody.data() };
	gss_buffer_desc unwrappedBody;
	uint32_t maj_stat, min_stat;
	maj_stat = gss_unwrap(
	    &min_stat, context, &buf, &unwrappedBody, nullptr, nullptr);
	if (GSS_ERROR(maj_stat)) {
	    if (maj_stat == GSS_S_CONTEXT_EXPIRED) {
		// XXX destroy context and re-init
	    }
	    reportError(mech, maj_stat, min_stat);
	}

	XdrMemory xm(unwrappedBody.value, unwrappedBody.length);
	uint32_t checkSeq;
	xm.getWord(checkSeq);
	xbody(&xm);
	gss_release_buffer(&min_stat, &unwrappedBody);

	if (checkSeq != seq) {
	    VLOG(1) << "Bad sequence number in reply:"
		    << " expected " << seq
		    << " received " << checkSeq;
	    return false;
	}
	break;
    }
    }
    return true;
}

// Build an rpc_gss_data_t as specified in RFC 2203 section 5.3.2.2
template <typename F>
static std::vector<uint8_t>
_encapsulateBody(const uint32_t seq, F&& xbody)
{
    XdrSizer xsz;
    xdr(seq, &xsz);
    xbody(&xsz);
    std::vector<uint8_t> body(xsz.size());
    XdrMemory xm(body.data(), body.size());
    xdr(seq, &xm);
    xbody(&xm);
    return std::move(body);
}

/// Encode a message body given the RPCSEC_GSS service and sequence
/// number
template <typename F>
void encodeBody(
    gss_ctx_id_t context, gss_OID mech,
    rpc_gss_service_t service, uint32_t seq,
    F&& xbody, XdrSink* xdrs)
{
    switch (service) {
    case rpcsec_gss_svc_none:
	xbody(xdrs);
	break;

    case rpcsec_gss_svc_integrity: {
	// Serialise the body and sequence number
	std::vector<uint8_t> body = _encapsulateBody(seq, xbody);

	// Checksum the body and write both to the channel
	uint32_t maj_stat, min_stat;
	gss_buffer_desc mic;
	gss_buffer_desc buf{ body.size(), body.data() };
	maj_stat = gss_get_mic(
	    &min_stat, context, GSS_C_QOP_DEFAULT, &buf, &mic);
	if (GSS_ERROR(maj_stat)) {
	    reportError(mech, maj_stat, min_stat);
	}
	xdr(body, xdrs);
	xdrs->putWord(mic.length);
	xdrs->putBytes(mic.value, mic.length);
	gss_release_buffer(&min_stat, &mic);

	break;
    }

    case rpcsec_gss_svc_privacy: {
	// Serialise the body and sequence number
	std::vector<uint8_t> body = _encapsulateBody(seq, xbody);

	// Wrap the body and write the wrap token to the channel
	uint32_t maj_stat, min_stat;
	gss_buffer_desc token;
	gss_buffer_desc buf{ body.size(), body.data() };
	maj_stat = gss_wrap(
	    &min_stat, context, true, GSS_C_QOP_DEFAULT, &buf,
	    nullptr, &token);
	if (GSS_ERROR(maj_stat)) {
	    reportError(mech, maj_stat, min_stat);
	}
	xdrs->putWord(token.length);
	xdrs->putBytes(token.value, token.length);
	gss_release_buffer(&min_stat, &token);

	break;
    }
    }
}

}

/// An RPC client using RPCSEC_GSS version 1 authentication
class GssClient: public Client
{
public:
    GssClient(uint32_t program, uint32_t version,
	      const std::string& principal,
	      const std::string& mechanism,
	      rpc_gss_service_t service);

    ~GssClient();

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
    gss_OID mech_;		  // GSS-API mechanism
    gss_ctx_id_t context_;	  // GSS-API context
    gss_cred_id_t cred_;	  // GSS-API credential
    gss_name_t principal_;	  // GSS-API name for remote principal
    uint32_t seq_window_;	  // size of the sequence window
    rpc_gss_cred_vers_1_t state_; // wire-level protocol state
    std::vector<uint8_t> verf_;	  // used to verify seq_window_
};

}
