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
