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

#include <stdexcept>
#include <string>

namespace oncrpc {

class Channel;

class RpcError: public std::runtime_error
{
public:
    RpcError(const std::string& what)
        : std::runtime_error(what)
    {
    }

    RpcError(const char* what)
        : std::runtime_error(what)
    {
    }
};

class XdrError: public RpcError
{
public:
    XdrError(const std::string& what)
        : RpcError(what)
    {
    }

    XdrError(const char* what)
        : RpcError(what)
    {
    }
};

class TimeoutError: public RpcError
{
public:
    TimeoutError()
        : RpcError("timeout")
    {
    }
};

class RestError: public RpcError
{
public:
    RestError(const std::string& what)
        : RpcError(what)
    {
    }

    RestError(const char* what)
        : RpcError(what)
    {
    }
};

/// Used to force Channel::call to re-send a message after a reconnect
class ResendMessage: public RpcError
{
public:
    ResendMessage()
        : RpcError("resend")
    {
    }
};

/// Used to stop a pre-generated service from sending a reply
class NoReply: public RpcError
{
public:
    NoReply()
        : RpcError("noreply")
    {
    }
};

/// Used to report GSS-API errors generated when validating replies
class GssError: public RpcError
{
public:
    GssError(const std::string& what)
	: RpcError(what)
    {
    }
};

/// MSG_ACCEPTED, PROG_UNAVAIL
class ProgramUnavailable: public RpcError
{
public:
    ProgramUnavailable(uint32_t prog);

    uint32_t prog() const { return prog_; }

private:
    uint32_t prog_;
};

/// MSG_ACCEPTED, PROC_UNAVAIL
class ProcedureUnavailable: public RpcError
{
public:
    ProcedureUnavailable(uint32_t prog);

    uint32_t proc() const { return proc_; }

private:
    uint32_t proc_;
};

/// MSG_ACCEPTED, PROG_MISMATCH
class VersionMismatch: public RpcError
{
public:
    VersionMismatch(uint32_t minver, uint32_t maxver);

    uint32_t minver() const { return minver_; }
    uint32_t maxver() const { return maxver_; }

private:
    uint32_t minver_;
    uint32_t maxver_;
};

/// MSG_ACCEPTED, GARBAGE_ARGS
class GarbageArgs: public RpcError
{
public:
    GarbageArgs();
};

/// MSG_ACCEPTED, SYSTEM_ERR
class SystemError: public RpcError
{
public:
    SystemError();
};

/// MSG_DENIED, RPC_MISMATCH
class ProtocolMismatch: public RpcError
{
public:
    ProtocolMismatch(uint32_t minver, uint32_t maxver);

    uint32_t minver() const { return minver_; }
    uint32_t maxver() const { return maxver_; }

private:
    uint32_t minver_;
    uint32_t maxver_;
};

/// MSG_DENIED, AUTH_ERROR
class AuthError: public RpcError
{
public:
    /// The argument is actually an auth_stat enum defined in rpcproto.h
    AuthError(int stat);

    int stat() const { return stat_; }

private:
    int stat_;
};

}
