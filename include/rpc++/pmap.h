// -*- c++ -*-

#pragma once

#include <rpc++/client.h>
#include <rpc++/xdr.h>

namespace oncrpc {

constexpr unsigned PMAPPROG = 100000;
constexpr unsigned PMAPVERS = 2;
constexpr unsigned PMAPPROC_NULL = 0;
constexpr unsigned PMAPPROC_GETPORT = 3;
constexpr unsigned PMAPPROC_DUMP = 4;

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

class PmapClient
{
public:
    PmapClient(std::shared_ptr<Client> client)
	: client_(client)
    {
    }

    void null()
    {
	client_->call(0, [](XdrSink*) {}, [](XdrSource*) {});
    }

    bool set(const mapping& args)
    {
	bool res;
	client_->call(1,
		      [&](XdrSink* xdrs) { xdr(args, xdrs); },
		      [&](XdrSource* xdrs) { xdr(res, xdrs); });
	return res;
    }

    bool unset(const mapping& args)
    {
	bool res;
	client_->call(2,
		      [&](XdrSink* xdrs) { xdr(args, xdrs); },
		      [&](XdrSource* xdrs) { xdr(res, xdrs); });
	return res;
    }

    uint32_t getport(const mapping& args)
    {
	uint32_t res;
	client_->call(3,
		      [&](XdrSink* xdrs) { xdr(args, xdrs); },
		      [&](XdrSource* xdrs) { xdr(res, xdrs); });
	return res;
    }

    std::unique_ptr<pmaplist> dump()
    {
	std::unique_ptr<pmaplist> res;
	client_->call(4,
		      [&](XdrSink* xdrs) { },
		      [&](XdrSource* xdrs) { xdr(res, xdrs); });
	return std::move(res);
    }

    call_result callit(call_args args)
    {
	call_result res;
	client_->call(5,
		      [&](XdrSink* xdrs) { xdr(args, xdrs); },
		      [&](XdrSource* xdrs) { xdr(res, xdrs); });
	return std::move(res);
    }

private:
    std::shared_ptr<Client> client_;
};

}
