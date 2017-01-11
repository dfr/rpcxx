/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
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
