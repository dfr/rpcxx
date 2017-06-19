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

#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include <rpc++/channel.h>
#include <rpc++/client.h>
#include <rpc++/errors.h>
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
        auto chan = Channel::open(host, 123456, 1, "tcp");
        for (auto i = int(GssService::NONE);
             i <= int(GssService::PRIVACY); ++i) {
            auto service = GssService(i);
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
