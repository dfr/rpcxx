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

#pragma once

#include <rpc++/rpcproto.h>

namespace oncrpc {

/// Unix-style credential associated with an RPC call
class Credential
{
public:
    /// Create a null credential uid and gid set to 'nobody'
    Credential();

    /// Create a cred with the given values
    Credential(
        int32_t uid, int32_t gid,
        std::vector<int32_t>&& gids,
        bool privileged = false);

    /// Copy constructor
    Credential(const Credential& other);

    /// Move constructor
    Credential(Credential&& other);

    /// Move operator
    Credential& operator=(Credential&& other);

    /// Set this credential to match the local user
    void setToLocal();

    auto uid() const { return uid_; }
    auto gid() const { return gid_; }
    auto& gids() const { return gids_; }
    auto privileged() const { return privileged_; }

    /// Return true if this cred has the given group
    bool hasgroup(int gid)  const;

private:
    int32_t uid_;
    int32_t gid_;
    std::vector<int32_t> gids_;
    bool privileged_;
};

/// An interface for classes which map user names to credentials
class CredMapper
{
public:
    virtual ~CredMapper() {}

    /// Map a user name to its matching credentials and return true if the
    /// user was found, false otherwise
    virtual bool lookupCred(const std::string& name, Credential& cred) = 0;
};

/// Look up users in the local password database
class LocalCredMapper: public CredMapper
{
public:
    ~LocalCredMapper() override;
    bool lookupCred(const std::string& name, Credential& cred) override;
};

}
