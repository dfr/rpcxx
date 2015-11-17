#include <pwd.h>

#include <rpc++/cred.h>

using namespace oncrpc;

Credential::Credential()
    : uid_(65534),
      gid_(65534),
      privileged_(false)
{
}

Credential::Credential(
    uint32_t uid, uint32_t gid, std::vector<uint32_t>&& gids, bool admin)
    : uid_(uid),
      gid_(gid),
      gids_(std::move(gids)),
      privileged_(admin)
{
}

Credential::Credential(Credential&& other)
    : uid_(other.uid_),
      gid_(other.gid_),
      gids_(std::move(other.gids_)),
      privileged_(other.privileged_)
{
}

Credential& Credential::operator=(Credential&& other)
{
    uid_ = other.uid_;
    gid_ = other.gid_;
    gids_ = std::move(other.gids_);
    return *this;
}

void Credential::setToLocal()
{
    uid_ = ::getuid();
    gid_ = ::getgid();
    std::vector<gid_t> gids;
    gids.resize(getgroups(0, nullptr));
    getgroups(gids.size(), gids.data());
    gids_.resize(gids.size());
    std::copy(gids.begin(), gids.end(), gids_.begin());
}

bool Credential::hasgroup(int gid) const
{
    if (gid_ == gid)
        return true;
    return std::find(gids_.begin(), gids_.end(), gid) != gids_.end();
}

LocalCredMapper::~LocalCredMapper()
{
}

bool LocalCredMapper::lookupCred(const std::string& name, Credential& cred)
{
#ifdef __APPLE__
    typedef int GID_T;
#else
    typedef gid_t GID_T;
#endif

    ::passwd pbuf;
    char buf[128];
    ::passwd* pwd;

    if (::getpwnam_r(name.c_str(), &pbuf, buf, sizeof(buf), &pwd) == 0) {
        std::vector<uint32_t> groups;
        static_assert(sizeof(GID_T) == sizeof(uint32_t), "sizeof(GID_T) != 4");
        groups.resize(NGROUPS_MAX);
        int len = NGROUPS_MAX;
        ::getgrouplist(name.c_str(), pwd->pw_gid,
            reinterpret_cast<GID_T*>(groups.data()), &len);
        groups.resize(len);
        cred = Credential(pwd->pw_uid, pwd->pw_gid, std::move(groups), false);
        return true;
    }
    else {
        return false;
    }
}
