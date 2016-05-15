// -*- c++ -*-

#pragma once

#include <cinttypes>
#include <vector>

#include <rpc++/xdr.h>

namespace oncrpc {

enum auth_flavor: uint32_t {
    AUTH_NONE  = 0,
    AUTH_SYS   = 1,
    AUTH_SHORT = 2,
    AUTH_DH    = 3,
    RPCSEC_GSS = 6,

    // Pseudo flavors used to select different Kerberos options
    RPCSEC_GSS_KRB5 = 390003,
    RPCSEC_GSS_KRB5I = 390004,
    RPCSEC_GSS_KRB5P = 390005
};

struct opaque_auth {
    auth_flavor flavor;
    bounded_vector<uint8_t, 400> auth_body;
};

template <typename XDR>
void xdr(RefType<opaque_auth, XDR> v, XDR* xdrs)
{
    xdr(reinterpret_cast<RefType<uint32_t, XDR>>(v.flavor), xdrs);
    xdr(v.auth_body, xdrs);
}

enum msg_type: uint32_t {
    CALL  = 0,
    REPLY = 1,
};

enum reply_stat: uint32_t {
    MSG_ACCEPTED = 0,
    MSG_DENIED   = 1,
};

enum accept_stat: uint32_t {
    SUCCESS       = 0,
    PROG_UNAVAIL  = 1,
    PROG_MISMATCH = 2,
    PROC_UNAVAIL  = 3,
    GARBAGE_ARGS  = 4,
    SYSTEM_ERR    = 5
};

enum reject_stat: uint32_t {
    RPC_MISMATCH = 0,
    AUTH_ERROR   = 1
};

enum auth_stat: uint32_t {
    AUTH_OK           = 0,  /* success                        */
    /*
     * failed at remote end
     */
    AUTH_BADCRED      = 1,  /* bad credential (seal broken)   */
    AUTH_REJECTEDCRED = 2,  /* client must begin new session  */
    AUTH_BADVERF      = 3,  /* bad verifier (seal broken)     */
    AUTH_REJECTEDVERF = 4,  /* verifier expired or replayed   */
    AUTH_TOOWEAK      = 5,  /* rejected for security reasons  */
    /*
     * failed locally
     */
    AUTH_INVALIDRESP  = 6,  /* bogus response verifier        */
    AUTH_FAILED       = 7,  /* reason unknown                 */
    /*
     * AUTH_KERB errors; deprecated.  See [RFC2695]
     */
    AUTH_KERB_GENERIC = 8,  /* kerberos generic error */
    AUTH_TIMEEXPIRE = 9,    /* time of credential expired */
    AUTH_TKT_FILE = 10,     /* problem with ticket file */
    AUTH_DECODE = 11,       /* can't decode authenticator */
    AUTH_NET_ADDR = 12,     /* wrong net address in ticket */
    /*
     * RPCSEC_GSS GSS related errors
     */
    RPCSEC_GSS_CREDPROBLEM = 13, /* no credentials for user */
    RPCSEC_GSS_CTXPROBLEM = 14   /* problem with context */
};

struct call_body {
    call_body() {}
    call_body(call_body&& other)
        : rpcvers(other.rpcvers),
          prog(other.prog),
          vers(other.vers),
          proc(other.proc),
          cred(std::move(other.cred)),
          verf(std::move(other.verf))
    {
    }
    call_body(
        uint32_t a, uint32_t b, uint32_t c, opaque_auth&& d, opaque_auth&& e)
        : prog(a),
          vers(b),
          proc(c),
          cred(std::move(d)),
          verf(std::move(e))
    {
    }
    uint32_t rpcvers = 2;
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    opaque_auth cred;
    opaque_auth verf;
};

template <typename XDR>
void xdr(RefType<call_body, XDR> v, XDR* xdrs)
{
    xdr(v.rpcvers, xdrs);
    xdr(v.prog, xdrs);
    xdr(v.vers, xdrs);
    xdr(v.proc, xdrs);
    xdr(v.cred, xdrs);
    xdr(v.verf, xdrs);
}

struct version_mismatch {
    uint32_t low;
    uint32_t high;
};

template <typename XDR>
void xdr(RefType<version_mismatch, XDR> v, XDR* xdrs)
{
    xdr(v.low, xdrs);
    xdr(v.high, xdrs);
}

struct accepted_reply {
    opaque_auth verf;
    accept_stat stat;
    version_mismatch mismatch_info;
};

template <typename XDR>
void xdr(RefType<accepted_reply, XDR> v, XDR* xdrs)
{
    xdr(v.verf, xdrs);
    xdr(reinterpret_cast<RefType<uint32_t, XDR>>(v.stat), xdrs);
    if (v.stat == PROG_MISMATCH)
        xdr(v.mismatch_info, xdrs);
}

struct rejected_reply {
    reject_stat stat;
    union {
        version_mismatch rpc_mismatch;
        auth_stat auth_error;
    };
};

template <typename XDR>
void xdr(RefType<rejected_reply, XDR> v, XDR* xdrs)
{
    xdr(reinterpret_cast<RefType<uint32_t, XDR>>(v.stat), xdrs);
    switch (v.stat) {
    case RPC_MISMATCH:
        xdr(v.rpc_mismatch, xdrs);
        break;
    case AUTH_ERROR:
        xdr(reinterpret_cast<RefType<uint32_t, XDR>>(v.auth_error), xdrs);
        break;
    default:
        break;
    }
}

struct reply_body {
    reply_stat stat;
    union reply {
        accepted_reply areply;
        rejected_reply rreply;
    };
    std::aligned_union<
        sizeof(reply), accepted_reply, rejected_reply>::type storage;
    bool hasValue = false;

    reply_body()
    {
    }

    reply_body(accepted_reply&& areply)
        : stat(MSG_ACCEPTED)
    {
        new(&storage) accepted_reply(std::move(areply));
        hasValue = true;
    }

    reply_body(rejected_reply&& rreply)
        : stat(MSG_DENIED)
    {
        new(&storage) rejected_reply(std::move(rreply));
        hasValue = true;
    }

    reply_body(reply_body&& other)
    {
        *this = std::move(other);
    }

    ~reply_body()
    {
        clear();
    }

    reply_body& operator=(reply_body&& other)
    {
        clear();
        stat = other.stat;
        if (other.hasValue) {
            switch (stat) {
            case MSG_ACCEPTED:
                new(&storage) accepted_reply(std::move(other.areply()));
                break;
            case MSG_DENIED:
                new(&storage) rejected_reply(std::move(other.rreply()));
                break;
            default:
                break;
            }
            hasValue = true;
            other.clear();
        }
        return *this;
    }

    void clear()
    {
        if (hasValue) {
            switch (stat) {
            case MSG_ACCEPTED:
                reinterpret_cast<accepted_reply*>(&storage)->~accepted_reply();
                break;
            case MSG_DENIED:
                reinterpret_cast<rejected_reply*>(&storage)->~rejected_reply();
                break;
            default:
                break;
            }
        }
        hasValue = false;
    }

    accepted_reply& areply()
    {
        assert(hasValue && stat == MSG_ACCEPTED);
        return *reinterpret_cast<accepted_reply*>(&storage);
    }

    const accepted_reply& areply() const
    {
        assert(hasValue && stat == MSG_ACCEPTED);
        return *reinterpret_cast<const accepted_reply*>(&storage);
    }

    rejected_reply& rreply()
    {
        assert(hasValue && stat == MSG_DENIED);
        return *reinterpret_cast<rejected_reply*>(&storage);
    }

    const rejected_reply& rreply() const
    {
        assert(hasValue && stat == MSG_DENIED);
        return *reinterpret_cast<const rejected_reply*>(&storage);
    }
};

static void xdr(const reply_body& v, XdrSink* xdrs)
{
    xdr(v.stat, xdrs);
    switch (v.stat) {
    case MSG_ACCEPTED:
        xdr(v.areply(), xdrs);
        break;
    case MSG_DENIED:
        xdr(v.rreply(), xdrs);
        break;
    default:
        break;
    }
}

static void xdr(reply_body& v, XdrSource* xdrs)
{
    v.clear();
    xdr(reinterpret_cast<uint32_t&>(v.stat), xdrs);
    switch (v.stat) {
    case MSG_ACCEPTED:
        new(&v.storage) accepted_reply();
        v.hasValue = true;
        xdr(v.areply(), xdrs);
        break;
    case MSG_DENIED:
        new(&v.storage) rejected_reply();
        v.hasValue = true;
        xdr(v.rreply(), xdrs);
        break;
    default:
        break;
    }
}

struct rpc_msg {
    rpc_msg()
        : xid(0)
    {
    }

    rpc_msg(uint32_t id, call_body&& cb)
        : xid(id),
          mtype(CALL)
    {
        new(&storage) call_body(std::move(cb));
        hasValue = true;
    }

    rpc_msg(uint32_t id, reply_body&& rb)
        : xid(id),
          mtype(REPLY)
    {
        new(&storage) reply_body(std::move(rb));
        hasValue = true;
    }

    rpc_msg(rpc_msg&& other)
    {
        *this = std::move(other);
    }

    ~rpc_msg()
    {
        clear();
    }

    rpc_msg& operator=(rpc_msg&& other)
    {
        clear();
        xid = other.xid;
        mtype = other.mtype;
        if (other.hasValue) {
            switch (mtype) {
            case CALL:
                new(&storage) call_body(std::move(other.cbody()));
                break;
            case REPLY:
                new(&storage) reply_body(std::move(other.rbody()));
                break;
            default:
                break;
            }
            hasValue = true;
            other.clear();
        }
        return *this;
    }

    void clear()
    {
        if (hasValue) {
            switch (mtype) {
            case CALL:
                reinterpret_cast<call_body*>(&storage)->~call_body();
                break;
            case REPLY:
                reinterpret_cast<reply_body*>(&storage)->~reply_body();
                break;
            default:
                break;
            }
            hasValue = false;
        }
    }

    uint32_t xid;
    msg_type mtype;
    union body {
        call_body cbody;
        reply_body rbody;
    };
    std::aligned_union<sizeof(body), call_body, reply_body>::type storage;
    bool hasValue = false;

    call_body& cbody()
    {
        assert(hasValue && mtype == CALL);
        return *reinterpret_cast<call_body*>(&storage);
    }

    const call_body& cbody() const
    {
        assert(hasValue && mtype == CALL);
        return *reinterpret_cast<const call_body*>(&storage);
    }

    reply_body& rbody()
    {
        assert(hasValue && mtype == REPLY);
        return *reinterpret_cast<reply_body*>(&storage);
    }

    const reply_body& rbody() const
    {
        assert(hasValue && mtype == REPLY);
        return *reinterpret_cast<const reply_body*>(&storage);
    }
};

static inline void xdr(const rpc_msg& v, XdrSink* xdrs)
{
    xdr(v.xid, xdrs);
    xdr(v.mtype, xdrs);
    switch (v.mtype) {
    case CALL:
        xdr(v.cbody(), xdrs);
        break;
    case REPLY:
        xdr(v.rbody(), xdrs);
        break;
    default:
        break;
    }
}

static inline void xdr(rpc_msg& v, XdrSource* xdrs)
{
    v.clear();
    xdr(v.xid, xdrs);
    xdr(reinterpret_cast<uint32_t&>(v.mtype), xdrs);
    switch (v.mtype) {
    case CALL:
        new(&v.storage) call_body();
        v.hasValue = true;
        xdr(v.cbody(), xdrs);
        break;
    case REPLY:
        new(&v.storage) reply_body();
        v.hasValue = true;
        xdr(v.rbody(), xdrs);
        break;
    default:
        break;
    }
}

}
