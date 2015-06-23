#include <random>

#include <rpc++/auth.h>
#include <rpc++/client.h>

using namespace oncrpc;

Client::Client(
    uint32_t program, uint32_t version, std::unique_ptr<Auth> auth)
    : program_(program),
      version_(version),
      auth_(auth ? std::move(auth) : std::make_unique<AuthNone>())
{
}

void
Client::validateAuth(Channel* chan)
{
    auth_->init(this, chan);
}

uint32_t
Client::processCall(
    Channel* chan, uint32_t xid, uint32_t proc,
    XdrSink* xdrs, std::function<void(XdrSink*)> xargs)
{
    auto res = auth_->encode(xid, program_, version_, proc, xdrs);
    xargs(xdrs);
    return res;
}

void
Client::processReply(
    uint32_t seq, accepted_reply& areply,
    XdrSource* xdrs, std::function<void(XdrSource*)> xresults)
{
    if (!auth_->validate(seq, areply.verf)) {
	// XXX refresh auth
	assert(false);
    }
    xresults(xdrs);
}

bool
Client::authError(auth_stat stat)
{
    return auth_->refresh(stat);
}
