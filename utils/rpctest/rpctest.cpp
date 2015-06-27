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

    try {
	auto chan = connectChannel(host, 123456, 1, "tcp");
	for (int i = rpcsec_gss_svc_none; i <= rpcsec_gss_svc_privacy; ++i) {
	    auto service = rpc_gss_service_t(i);
	    auto cl = make_shared<GssClient>(
		123456, 1, "host@" + host, "krb5", service);
	    const uint32_t ival = 42;
	    uint32_t oval;
	    chan->call(
		cl.get(), 1,
		[&](XdrSink* xdrs) { xdr(ival, xdrs); },
		[&](XdrSource* xdrs) { xdr(oval, xdrs); });
	    cout << oval << endl;
	}
    }
    catch (std::system_error& e) {
	cout << "rpctest: " << e.what() << endl;
	exit(1);
    }
    catch (RpcError& e) {
	cout << "rpctest: " << e.what() << endl;
	exit(1);
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
