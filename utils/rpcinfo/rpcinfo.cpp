#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <rpc++/client.h>
#include <rpc++/pmap.h>
#include <rpc++/rpcbind.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

using namespace oncrpc;
using namespace std;

template <int... Widths>
class TableFormatter
{
public:
    TableFormatter(ostream& str) : str_(str)
    {
    }

    template <typename... T>
    void operator()(T... fields)
    {
	format(make_pair(Widths, fields)...);
	str_ << endl;
    }

private:
    template <typename T>
    void format(const pair<int, T>& field)
    {
	str_ << left << setw(field.first) << field.second;
    }

    template <typename T, typename... Rest>
    void format(const pair<int, T>& field, Rest... rest)
    {
	format(field);
	format(rest...);
    }

    ostream& str_;
};

struct RpcEntry
{
    string name;
    unsigned program;
    vector<string> aliases;
};
map<uint32_t, RpcEntry> rpcentries;

enum Mode {
    ListServices,
    ListServicesV2,
    Ping,
};

struct options
{
    Mode mode = ListServices;
    string transport;
    string serviceAddress;
    bool broadcast = false;
    bool deleteRegistration = false;
    bool listEntries = false;
    bool listStats = false;
    int port = 111;
    bool probeUsingPortmap = false;
    bool shortFormat = false;
    bool tcpProbe = true;
    bool udpProbe = false;
};

void usage()
{
    cerr << "usage: rpcinfo [-m | -s] [host]" << endl
	 << "rpcinfo -p [host]" << endl
	 << "rpcinfo -T netid host prognum [versnum]" << endl
	 << "rpcinfo -l host prognum versnum" << endl
	 << "rpcinfo [-n portnum] -u | -t host prognum [versnum]" << endl
	 << "rpcinfo -a serv_address -T netid prognum [version]" << endl
	 << "rpcinfo -b prognum versnum" << endl
	 << "rpcinfo -d [-T netid] prognum versnum});" << endl;
    exit(1);
}


void readEtcRpc()
{
    ifstream f("/etc/rpc");
    char buf[512];
    unsigned lineno = 0;
    while (f.getline(buf, sizeof(buf))) {
	string line = buf;
	lineno++;
	auto comment = line.find('#');
	if (comment != string::npos)
	    line = line.substr(0, comment);
	if (line.size() == 0)
	    continue;
	vector<string> fields;
	istringstream iss(line);
	copy(istream_iterator<string>(iss),
	     istream_iterator<string>(),
	     back_inserter(fields));
	if (fields.size() >= 2) {
	    auto entry = RpcEntry{
		fields[0], unsigned(stoi(fields[1])),
		vector<string>(fields.begin() + 2, fields.end())};
	    rpcentries[entry.program] = entry;
	}
	else {
	    cerr << "rpcinfo: ignoring malformed line "
		 << lineno << " in /etc/rpc" << endl;
	}
    }
}

string lookupProgram(unsigned prog)
{
    auto p = rpcentries.find(prog);
    if (p != rpcentries.end())
	return p->second.name;
    return "???";
}

std::shared_ptr<Client> connectClient(
    const std::string& host, const options& opts,
    uint32_t prog, uint32_t vers)
{
    string service = std::to_string(opts.port);

    int sock;
    int socktype = SOCK_STREAM;
    try {
	sock = connectSocket(host, service, socktype);
    }
    catch (std::runtime_error& e) {
	cout << "rpcinfo: " << e.what() << endl;
	exit(1);
    }

    if (socktype == SOCK_STREAM)
	return std::make_shared<StreamClient>(sock, prog, vers);
    else
	return std::make_shared<DatagramClient>(sock, prog, vers);
}

std::shared_ptr<Client> connectClient(
    const AddressInfo* addr, uint32_t prog, uint32_t vers)
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
	cerr << "rpcinfo: " << cause << ": " << ec.message() << endl;
	exit(1);
    }

    if (addr->socktype == SOCK_STREAM)
	return std::make_shared<StreamClient>(s, prog, vers);
    else
	return std::make_shared<DatagramClient>(s, prog, vers);
}

template <typename T>
string
csvFormat(const set<T>& cont)
{
    ostringstream ss;
    copy(cont.begin(), cont.end(), ostream_iterator<T>(ss, ","));
    auto s = ss.str();
    return s.substr(0, s.size() - 1);
}

void listServices(const vector<string>& args, const options& opts)
{
    if (args.size() > 1)
	usage();

    string host;
    if (args.size() == 1)
	host = args[0];
    else
	host = "localhost";

    RpcBindClient cl(connectClient(host, opts, RPCBPROG, RPCBVERS));

    if (opts.shortFormat) {
	struct programInfo
	{
	    set<uint32_t> versions;
	    set<string> netids;
	    string owner;
	};
	map<uint32_t, programInfo> programs;

	auto p0 = cl.dump();
	for (auto p = p0.get(); p; p = p->rpcb_next.get()) {
	    const auto& map = p->rpcb_map;
	    auto& prog = programs[map.r_prog];
	    prog.versions.insert(map.r_vers);
	    prog.netids.insert(map.r_netid);
	    prog.owner = map.r_owner;
	}

	TableFormatter<10, 12, 32, 12, 10> tf(cout);
	tf("program", "version(s)", "netid(s)", "service", "owner");
	for (const auto& p: programs) {
	    tf(p.first,
	       csvFormat(p.second.versions),
	       csvFormat(p.second.netids),
	       lookupProgram(p.first),
	       p.second.owner);
	}
    }
    else {
	TableFormatter<10, 10, 10, 24, 12, 12> tf(cout);
	tf("program", "version", "netid", "address", "service", "owner");

	auto p0 = cl.dump();
	for (auto p = p0.get(); p; p = p->rpcb_next.get()) {
	    const auto& map = p->rpcb_map;
	    tf(map.r_prog, map.r_vers, map.r_netid, map.r_addr,
	       lookupProgram(map.r_prog), map.r_owner);
	}
    }
}

void listServicesV2(const vector<string>& args, const options& opts)
{
    if (args.size() > 1)
	usage();

    string host;
    if (args.size() == 1)
	host = args[0];
    else
	host = "localhost";

    PmapClient cl(connectClient(host, opts, PMAPPROG, PMAPVERS));

    TableFormatter<10, 6, 7, 7, 9> tf(cout);
    tf("program", "vers", "proto", "port", "service");

    auto p0 = cl.dump();
    for (auto p = p0.get(); p; p = p->next.get()) {
	auto& map = p->map;
	string prot;
	switch (map.prot) {
	case IPPROTO_TCP:
	    prot = "tcp";
	    break;

	case IPPROTO_UDP:
	    prot = "udp";
	    break;
	case 7:		// IPPROTO_ST
	    prot = "local";
	    break;
	default:
	    prot = std::to_string(map.prot);
	}
	tf(map.prog, map.vers, prot, map.port, lookupProgram(map.prog));
    }
}

void ping(const vector<string>& args, const options& opts)
{
    if (args.size() < 2 || args.size() > 3)
	usage();

    string host = args[0];
    uint32_t program = stoi(args[1]);

    // Connect to rpcbind on the remote host
    RpcBindClient rpcbind(connectClient(host, opts, RPCBPROG, RPCBVERS));

    rpcb rb{ program, 0 };
    auto uaddr = rpcbind.getaddr(rb);

    if (uaddr == "") {
	cout << "rpcinfo: RPC: Program not registered" << endl;
	exit(1);
    }
    auto addr = uaddr2taddr(PF_UNSPEC, SOCK_STREAM, uaddr);

    vector<uint32_t> versions;
    if (args.size() == 3)
	versions.push_back(stoi(args[2]));
    else {
	// Use a null call on version 0 to get the version range
	auto cl = connectClient(addr.get(), program, 0);
	try {
	    cl->call(0, [](XdrSink*){}, [](XdrSource*){});
	}
	catch (VersionMismatch& e) {
	    for (auto i = e.minver(); i != e.maxver(); i++)
		versions.push_back(i);
	}
	catch (RpcError& e) {
	    cout << "rpcinfo: " << e.what() << endl;
	    exit(1);
	}
    }

    for (auto version: versions) {
	auto cl = connectClient(addr.get(), program, version);
	try {
	    cl->call(0, [](XdrSink*){}, [](XdrSource*){});
	}
	catch (RpcError& e) {
	    cout << "rpcinfo: " << e.what() << endl;
	    exit(1);
	}
	cout << "program " << program << " version " << version
	     << " ready and waiting" << endl;
    }
}

int
main(int argc, char* const argv[])
{
    options opts;

    readEtcRpc();

    int opt;
    while ((opt = getopt(argc, argv, "T:a:bdlmn:pstu")) != -1) {
	switch (opt) {
	case 'T':
	    opts.transport = optarg;
	    opts.mode = Ping;
	    break;
	case 'a':
	    opts.serviceAddress = optarg;
	    break;
	case 'b':
	    opts.broadcast = true;
	    break;
	case 'd':
	    opts.deleteRegistration = true;
	    break;
	case 'l':
	    opts.listEntries = true;
	    break;
	case 'm':
	    opts.listStats = true;
	    break;
	case 'n':
	    opts.port = stoi(optarg);
	    break;
	case 'p':
	    opts.mode = ListServicesV2;
	    break;
	case 's':
	    opts.shortFormat = true;
	    break;
	case 't':
	    opts.tcpProbe = true;
	    break;
	case 'u':
	    opts.udpProbe = true;
	    break;
	case '?':
	default:
	    usage();
	}
    }
    argc -= optind;
    argv += optind;

    vector<string> args;
    for (int i = 0; i < argc; i++)
	args.push_back(argv[i]);

    switch (opts.mode) {
    case ListServices:
	listServices(args, opts);
	break;

    case ListServicesV2:
	listServicesV2(args, opts);
	break;

    case Ping:
	ping(args, opts);
	break;

    default:
	usage();
    }

    return 0;
}
