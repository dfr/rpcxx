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

#include <condition_variable>
#include <mutex>

#ifdef __APPLE__
#include <GSS/GSS.h>
#include <CoreFoundation/CoreFoundation.h>
#else
#include <gssapi/gssapi.h>
#endif

#include <rpc++/client.h>
#include <rpc++/rpcsec_gss.h>

namespace oncrpc {

namespace _detail {

[[noreturn]] void reportError(gss_OID mech, uint32_t maj, uint32_t min);

/// Log a bad sequence number
void badSequence(uint32_t seq, uint32_t checkSeq);

/// Decode a message body given the RPCSEC_GSS service and sequence
/// number
template <typename F>
bool decodeBody(
    gss_ctx_id_t context, gss_OID mech,
    GssService service, uint32_t seq,
    F&& xbody, XdrSource* xdrs)
{
    switch (service) {
    case GssService::NONE:
        xbody(xdrs);
        break;

    case GssService::INTEGRITY: {
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
            badSequence(seq, checkSeq);
            return false;
        }
        break;
    }

    case GssService::PRIVACY: {
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
            badSequence(seq, checkSeq);
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
    return body;
}

/// Encode a message body given the RPCSEC_GSS service and sequence
/// number
template <typename F>
void encodeBody(
    gss_ctx_id_t context, gss_OID mech,
    GssService service, uint32_t seq,
    F&& xbody, XdrSink* xdrs)
{
    switch (service) {
    case GssService::NONE:
        xbody(xdrs);
        break;

    case GssService::INTEGRITY: {
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

    case GssService::PRIVACY: {
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
    /// Create a client using default initiator credentials
    GssClient(uint32_t program, uint32_t version,
              const std::string& principal,
              const std::string& mechanism,
              GssService service);

    /// Create a client using the given initiator
    GssClient(uint32_t program, uint32_t version,
            const std::string& initiator,
            const std::string& principal,
            const std::string& mechanism,
            GssService service);

    ~GssClient();

    /// Set default service. Note: since this is separate from the API for
    /// making RPC calls, multi-threaded applications cannot rely on this
    /// to choose a service for subsequent calls when more than one thread
    /// is using the client.
    void setService(GssService service);

    // Client overrides
    int validateAuth(Channel* chan, bool revalidate) override;
    bool processCall(
        uint32_t xid, int gen, uint32_t proc, XdrSink* xdrs,
        std::function<void(XdrSink*)> xargs, Protection prot,
        uint32_t& seq) override;
    bool processReply(
        uint32_t seq, int gen, accepted_reply& areply,
        XdrSource* xdrs, std::function<void(XdrSource*)> xresults,
        Protection prot) override;
    bool authError(int gen, int stat) override;

private:
    GssService getService(Protection prot) const
    {
        switch (prot) {
        case Protection::DEFAULT:
            return defaultService_;
        case Protection::NONE:
            return GssService::NONE;
        case Protection::INTEGRITY:
            return GssService::INTEGRITY;
        case Protection::PRIVACY:
            return GssService::PRIVACY;
        }
    }

    std::mutex mutex_;            // locks context_, state_, inflightCalls_
    std::condition_variable cv_;  // used to wait for space in sequence window
    int generation_ = 0;          // used to track when context re-initialises
    gss_OID mech_;                // GSS-API mechanism
    gss_ctx_id_t context_;        // GSS-API context
    gss_cred_id_t cred_;          // GSS-API credential
    gss_name_t principal_;        // GSS-API name for remote principal
    uint32_t sequenceWindow_;     // size of the sequence window
    uint32_t sequence_;           // next sequence number to use
    bool established_;            // true if we have completed context init
    GssService defaultService_;   // default service
    std::vector<uint8_t> handle_; // server client handle
    uint32_t inflightCalls_ = 0;
};

}
