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
