/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <sstream>

#include <rpc++/errors.h>
#include <rpc++/rpcproto.h>

using namespace oncrpc;

ProgramUnavailable::ProgramUnavailable(uint32_t prog)
    : RpcError([prog]() {
            std::ostringstream msg;
            msg << "RPC: program " << prog << " unavailable";
            return msg.str();
        }()),
      prog_(prog)
{
}

ProcedureUnavailable::ProcedureUnavailable(uint32_t proc)
    : RpcError([proc]() {
            std::ostringstream msg;
            msg << "RPC: procedure " << proc << " unavailable";
            return msg.str();
        }()),
      proc_(proc)
{
}

VersionMismatch::VersionMismatch(uint32_t minver, uint32_t maxver)
    : RpcError([minver, maxver]() {
            std::ostringstream msg;
            msg << "RPC: program version mismatch: low version = "
                << minver << ", high version = " << maxver;
            return msg.str();
        }()),
      minver_(minver),
      maxver_(maxver)
{
}

GarbageArgs::GarbageArgs()
    : RpcError("RPC: garbage args")
{
}

SystemError::SystemError()
    : RpcError("RPC: remote system error")
{
}

ProtocolMismatch::ProtocolMismatch(uint32_t minver, uint32_t maxver)
    : RpcError([minver, maxver]() {
            std::ostringstream msg;
            msg << "RPC: protocol version mismatch: low version = "
                << minver << ", high version = " << maxver;
            return msg.str();
        }()),
      minver_(minver),
      maxver_(maxver)
{
}

AuthError::AuthError(int stat)
    : RpcError([stat]() {
            static const char* str[] = {
                "AUTH_OK",
                "AUTH_BADCRED",
                "AUTH_REJECTEDCRED",
                "AUTH_BADVERF",
                "AUTH_REJECTEDVERF",
                "AUTH_TOOWEAK",
                "AUTH_INVALIDRESP",
                "AUTH_FAILED",
                "AUTH_KERB",
                "AUTH_TIMEEXPIRE",
                "AUTH_TKT",
                "AUTH_DECODE",
                "AUTH_NET",
                "RPCSEC_GSS_CREDPROBLEM",
                "RPCSEC_GSS_CTXPROBLEM"
            };
            std::ostringstream msg;
            if (uint32_t(stat) < AUTH_OK ||
                uint32_t(stat) > RPCSEC_GSS_CTXPROBLEM)
                msg << "RPC: unknown auth error: " << int(stat);
            else
                msg << "RPC: auth error: " << str[stat];
            return msg.str();
        }()),
      stat_(stat)
{
}
