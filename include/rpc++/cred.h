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
