/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <pwd.h>

#include <rpc++/cred.h>
#include <gtest/gtest.h>

using namespace oncrpc;
using namespace std;

class CredTest: public ::testing::Test
{
};

TEST_F(CredTest, Hasgroup)
{
    EXPECT_EQ(true, Credential(99, 99, {}).hasgroup(99));
    EXPECT_EQ(true, Credential(99, 99, {100}).hasgroup(100));
    EXPECT_EQ(false, Credential(99, 99, {100}).hasgroup(101));
}

TEST_F(CredTest, LocalCredMapper)
{
    auto pw = ::getpwuid(::getuid());
    EXPECT_NE(nullptr, pw);

    Credential cred;
    LocalCredMapper mapper;
    EXPECT_EQ(true, mapper.lookupCred(pw->pw_name, cred));
    EXPECT_EQ(::getuid(), cred.uid());
    EXPECT_EQ(::getgid(), cred.gid());

    std::vector<gid_t> gids;
    gids.resize(getgroups(0, nullptr));
    getgroups(gids.size(), gids.data());
    set<gid_t> groups;
    for (auto gid: gids)
        groups.insert(gid);
    for (auto gid: cred.gids())
        EXPECT_GT(groups.count(gid), 0);
}
