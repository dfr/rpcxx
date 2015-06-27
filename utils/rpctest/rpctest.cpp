#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/gss.h>
#include <rpc++/rpcbind.h>

using namespace oncrpc;
using namespace std;

[[noreturn]] static void
usage(void)
{
    cout << "rpctest client | server" << endl;
    exit(1);
}

std::shared_ptr<Channel> connectChannel(const AddressInfo* addr)
{
    string cause = "";
    std::error_code ec;
    int s;

    s = socket(addr->family, addr->socktype, addr->protocol);
    if (s < 0) {
	cause = "socket";
	ec = std::error_code(errno, std::system_category());
    }
    else {
	if (connect(s, addr->addr, addr->addrlen) < 0) {
	    cause = "connect";
	    ec = std::error_code(errno, std::system_category());
	    close(s);
	    s = -1;
	}
    }

    if (s == -1) {
	cerr << "rpctest: " << cause << ": " << ec.message() << endl;
	exit(1);
    }

    if (addr->socktype == SOCK_STREAM)
	return std::make_shared<StreamChannel>(s);
    else
	return std::make_shared<DatagramChannel>(s);
}

std::shared_ptr<Channel> connectChannel(
    const std::string& host, uint32_t prog, uint32_t vers)
{
    string service = "sunrpc";
    int sock;
    try {
	sock = connectSocket(host, service, SOCK_STREAM);
    }
    catch (std::runtime_error& e) {
	cout << "rpctest: " << e.what() << endl;
	exit(1);
    }

    auto rpcbind = RpcBind(std::make_shared<StreamChannel>(sock));
    auto uaddr = rpcbind.getaddr(rpcb{prog, vers, "", "", ""});
    if (uaddr == "") {
	cout << "rpctest: can't connect to program=" << prog
	     << ", version=" << vers
	     << " on " << host << endl;
	exit(1);
    }
    return connectChannel(uaddr2taddr(PF_UNSPEC, SOCK_STREAM, uaddr).get());
}

int test_client(const vector<string>& args)
{
    if (args.size() > 2)
	usage();

    string host;
    if (args.size() == 2)
	host = args[1];
    else {
	char buf[128];
	::gethostname(buf, sizeof(buf));
	host = buf;
    }

    auto chan = connectChannel(host, 123456, 1);
    for (int i = rpcsec_gss_svc_none; i <= rpcsec_gss_svc_privacy; ++i) {
	auto service = rpc_gss_service_t(i);
	auto cl = make_shared<GssClient>(
	    123456, 1, "host@" + host, "krb5", service);
	uint32_t val = 42;
	chan->call(
	    cl.get(), 1,
	    [&](XdrSink* xdrs) { xdr(val, xdrs); },
	    [&](XdrSource* xdrs) { xdr(val, xdrs); });
	cout << val << endl;
    }
    return 0;
}

int test_server(const vector<string>& args)
{
    usage();
}

int main(int argc, const char** argv)
{
    if (argc < 2)
	usage();

    vector<string> args;
    for (int i = 1; i < argc; i++)
	args.push_back(argv[i]);

    if (args[0] == "client")
	return test_client(args);
    if (string(argv[1]) == "server")
	return test_server(args);
    else
	usage();

    return 0;
}
