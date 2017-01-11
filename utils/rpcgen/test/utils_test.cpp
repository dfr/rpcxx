/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "utils/rpcgen/utils.h"

using namespace oncrpc::rpcgen;
using namespace std;

TEST(UtilsTest, ParseNamespaces)
{
    EXPECT_THROW(parseNamespaces(""), runtime_error);
    EXPECT_THROW(parseNamespaces("foo bar"), runtime_error);
    EXPECT_THROW(parseNamespaces("foo::"), runtime_error);
    EXPECT_EQ(vector<string>{"foo"}, parseNamespaces("foo"));
    EXPECT_EQ((vector<string>{"foo", "bar"}), parseNamespaces("foo::bar"));
    EXPECT_EQ((
        vector<string>{"_foo12", "bar"}), parseNamespaces("_foo12::bar"));
}

TEST(UtilsTest, ParseIdentifier)
{
    EXPECT_EQ(vector<string>{"foo"}, parseIdentifier("foo"));
    EXPECT_EQ(vector<string>{"foo"}, parseIdentifier("Foo"));
    EXPECT_EQ(vector<string>{"foo"}, parseIdentifier("FOO"));
    EXPECT_EQ((vector<string>{"foo", "bar"}), parseIdentifier("fooBar"));
    EXPECT_EQ((vector<string>{"foo", "bar"}), parseIdentifier("FooBar"));
    EXPECT_EQ((vector<string>{"foo", "bar"}), parseIdentifier("foo_bar"));
    EXPECT_EQ((vector<string>{"foo", "bar"}), parseIdentifier("FOO_BAR"));
}

TEST(UtilsTest, FormatIdentifier)
{
    EXPECT_EQ("fooBar", formatIdentifier(LCAMEL, {"foo", "bar"}));
    EXPECT_EQ("FooBar", formatIdentifier(UCAMEL, {"foo", "bar"}));
    EXPECT_EQ("foo_bar", formatIdentifier(LUNDERSCORE, {"foo", "bar"}));
    EXPECT_EQ("FOO_BAR", formatIdentifier(UUNDERSCORE, {"foo", "bar"}));
}
