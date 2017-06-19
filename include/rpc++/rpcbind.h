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
#include <rpc++/client.h>
#include <rpc++/xdr.h>

namespace oncrpc {

/*
 * rpcb_prot.x
 * rpcbind protocol, versions 3 and 4, in RPC Language
 */

/*
 * rpcbind address for TCP/UDP
 */
constexpr int RPCB_PORT = 111;

/*
 * A mapping of (program, version, network ID) to address
 *
 * The network identifier  (r_netid):
 * This is a string that represents a local identification for a
 * network. This is defined by a system administrator based on local
 * conventions, and cannot be depended on to have the same value on
 * every system.
 */
struct rpcb {
    uint32_t r_prog;            /* program number */
    uint32_t r_vers;            /* version number */
    std::string r_netid;        /* network id */
    std::string r_addr;         /* universal address */
    std::string r_owner;        /* owner of this service */
};

template <typename XDR>
void xdr(RefType<rpcb, XDR> v, XDR* xdrs)
{
    xdr(v.r_prog, xdrs);
    xdr(v.r_vers, xdrs);
    xdr(v.r_netid, xdrs);
    xdr(v.r_addr, xdrs);
    xdr(v.r_owner, xdrs);
}

struct rp__list {
    rpcb rpcb_map;
    std::unique_ptr<rp__list> rpcb_next;
};

template <typename XDR>
void xdr(RefType<rp__list, XDR> v, XDR* xdrs)
{
    xdr(v.rpcb_map, xdrs);
    xdr(v.rpcb_next, xdrs);
}

typedef std::unique_ptr<rp__list> rpcblist_ptr; /* results of RPCBPROC_DUMP */

/*
 * Arguments of remote calls
 */
struct rpcb_rmtcallargs {
    uint32_t prog;              /* program number */
    uint32_t vers;              /* version number */
    uint32_t proc;              /* procedure number */
    std::vector<uint8_t> args;  /* argument */
};

template <typename XDR>
void xdr(RefType<rpcb_rmtcallargs, XDR> v, XDR* xdrs)
{
    xdr(v.prog, xdrs);
    xdr(v.vers, xdrs);
    xdr(v.proc, xdrs);
    xdr(v.args, xdrs);
}

/*
 * Results of the remote call
 */
struct rpcb_rmtcallres {
    std::string addr;           /* remote universal address */
    std::vector<uint8_t> results; /* result */
};

template <typename XDR>
void xdr(RefType<rpcb_rmtcallres, XDR> v, XDR* xdrs)
{
    xdr(v.addr, xdrs);
    xdr(v.results, xdrs);
}

/*
 * rpcb_entry contains a merged address of a service on a particular
 * transport, plus associated netconfig information.  A list of
 * rpcb_entry items is returned by RPCBPROC_GETADDRLIST.  The meanings
 * and values used for the r_nc_* fields are given below.
 *
 * The network identifier  (r_nc_netid):

 *   This is a string that represents a local identification for a
 *   network.  This is defined by a system administrator based on
 *   local conventions, and cannot be depended on to have the same
 *   value on every system.
 *
 * Transport semantics (r_nc_semantics):
 *  This represents the type of transport, and has the following values:
 *     NC_TPI_CLTS     (1)      Connectionless
 *     NC_TPI_COTS     (2)      Connection oriented
 *     NC_TPI_COTS_ORD (3)      Connection oriented with graceful close
 *     NC_TPI_RAW      (4)      Raw transport
 *
 * Protocol family (r_nc_protofmly):
 *   This identifies the family to which the protocol belongs.  The
 *   following values are defined:
 *     NC_NOPROTOFMLY   "-"
 *     NC_LOOPBACK      "loopback"
 *     NC_INET          "inet"
 *     NC_IMPLINK       "implink"
 *     NC_PUP           "pup"
 *     NC_CHAOS         "chaos"
 *     NC_NS            "ns"
 *     NC_NBS           "nbs"
 *     NC_ECMA          "ecma"
 *     NC_DATAKIT       "datakit"
 *     NC_CCITT         "ccitt"
 *     NC_SNA           "sna"
 *     NC_DECNET        "decnet"
 *     NC_DLI           "dli"
 *     NC_LAT           "lat"
 *     NC_HYLINK        "hylink"
 *     NC_APPLETALK     "appletalk"
 *     NC_NIT           "nit"
 *     NC_IEEE802       "ieee802"
 *     NC_OSI           "osi"
 *     NC_X25           "x25"
 *     NC_OSINET        "osinet"
 *     NC_GOSIP         "gosip"
 *
 * Protocol name (r_nc_proto):
 *   This identifies a protocol within a family.  The following are
 *   currently defined:
 *      NC_NOPROTO      "-"
 *      NC_TCP          "tcp"
 *      NC_UDP          "udp"
 *      NC_ICMP         "icmp"
 */
struct rpcb_entry {
    std::string          r_maddr;            /* merged address of service */
    std::string          r_nc_netid;         /* netid field */
    uint32_t             r_nc_semantics;     /* semantics of transport */
    std::string          r_nc_protofmly;     /* protocol family */
    std::string          r_nc_proto;         /* protocol name */
};

/*
 * A list of addresses supported by a service.
 */
struct rpcb_entry_list {
    rpcb_entry rpcb_entry_map;
    std::unique_ptr<rpcb_entry_list> rpcb_entry_next;
};

typedef std::unique_ptr<rpcb_entry_list> rpcb_entry_list_ptr;


/*
 * rpcbind statistics
 */

constexpr int rpcb_highproc_2 = 5;
constexpr int rpcb_highproc_3 = 8;
constexpr int rpcb_highproc_4 = 12;

constexpr int RPCBSTAT_HIGHPROC = 13; /* # of procs in rpcbind V4 plus one */
constexpr int RPCBVERS_STAT     = 3; /* provide only for rpcbind V2, V3 and V4 */
constexpr int RPCBVERS_4_STAT   = 2;
constexpr int RPCBVERS_3_STAT   = 1;
constexpr int RPCBVERS_2_STAT   = 0;

/* Link list of all the stats about getport and getaddr */
struct rpcbs_addrlist {
    uint32_t prog;
    uint32_t vers;
    int success;
    int failure;
    std::string netid;
    std::unique_ptr<rpcbs_addrlist> next;
};

/* Link list of all the stats about rmtcall */
struct rpcbs_rmtcalllist {
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    int success;
    int failure;
    int indirect;    /* whether callit or indirect */
    std::string netid;
    std::unique_ptr<rpcbs_rmtcalllist> next;
};

typedef int rpcbs_proc[RPCBSTAT_HIGHPROC];
typedef std::unique_ptr<rpcbs_addrlist> rpcbs_addrlist_ptr;
typedef std::unique_ptr<rpcbs_rmtcalllist> rpcbs_rmtcalllist_ptr;

struct rpcb_stat {
    rpcbs_proc              info;
    int                     setinfo;
    int                     unsetinfo;
    rpcbs_addrlist_ptr      addrinfo;
    rpcbs_rmtcalllist_ptr   rmtinfo;
};

/*
 * One rpcb_stat structure is returned for each version of rpcbind
 * being monitored.
 */

typedef rpcb_stat rpcb_stat_byvers[RPCBVERS_STAT];

/*
 * netbuf structure, used to store the transport specific form of
 * a universal transport address.
 */
struct netbuf {
    uint32_t maxlen;
    std::vector<uint8_t> buf;
};

template <typename XDR>
void xdr(RefType<netbuf, XDR> v, XDR* xdrs)
{
    xdr(v.maxlen, xdrs);
    xdr(v.buf, xdrs);
}

/*
 * rpcbind procedures
 */
constexpr int RPCBPROG = 100000;
constexpr int RPCBVERS = 3;
constexpr int RPCBVERS4 = 4;

class RpcBind
{
public:
    RpcBind(std::shared_ptr<Channel> channel)
        : channel_(channel),
          client_(std::make_shared<Client>(RPCBPROG, RPCBVERS))
    {
    }

    void null()
    {
        channel_->call(client_.get(), 0, [](XdrSink*) {}, [](XdrSource*) {});
    }

    bool set(const rpcb& args)
    {
        bool res;
        channel_->call(client_.get(), 1,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    bool unset(const rpcb& args)
    {
        bool res;
        channel_->call(client_.get(), 2,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    std::string getaddr(const rpcb& args)
    {
        std::string res;
        channel_->call(client_.get(), 3,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    rpcblist_ptr dump()
    {
        rpcblist_ptr res;
        channel_->call(client_.get(), 4,
                      [&](XdrSink* xdrs) { },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    rpcb_rmtcallres callit(const rpcb_rmtcallargs& args)
    {
        rpcb_rmtcallres res;
        channel_->call(client_.get(), 5,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    uint32_t gettime()
    {
        uint32_t res;
        channel_->call(client_.get(), 6,
                      [&](XdrSink* xdrs) { },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    netbuf uaddr2taddr(const std::string& args)
    {
        netbuf res;
        channel_->call(client_.get(), 7,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

    std::string taddr2uaddr(const netbuf& args)
    {
        std::string res;
        channel_->call(client_.get(), 7,
                      [&](XdrSink* xdrs) { xdr(args, xdrs); },
                      [&](XdrSource* xdrs) { xdr(res, xdrs); });
        return res;
    }

private:
    std::shared_ptr<Channel> channel_;
    std::shared_ptr<Client> client_;
};

#if 0
@program(RPCBPROG, RPCBVERS4)
interface RPCBind4
{
    @proc(0) void null_();
    @proc(1) bool set(rpcb);
    @proc(2) bool unset(rpcb);
    @proc(3) std::string getaddr(rpcb);
    @proc(4) rpcblist_ptr dump();
    /*
     * NOTE: bcast has the same functionality as callit;
     * the new name is intended to indicate that this
     * procedure should be used for broadcast RPC, and
     * indirect should be used for indirect calls.
     */
    @proc(5) rpcb_rmtcallres bcast(rpcb_rmtcallargs);
    @proc(6) uint gettime();
    @proc(7) netbuf uaddr2taddr(std::string);
    @proc(8) std::string taddr2uaddr(netbuf);
    @proc(9) std::string getversaddr(rpcb);
    @proc(10) rpcb_rmtcallres indirect(rpcb_rmtcallargs);
    @proc(11) rpcb_entry_list_ptr getaddrlist(rpcb);
    @proc(12) rpcb_stat_byvers getstat();
}
#endif

}
