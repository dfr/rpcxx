/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <cassert>
#include <memory>

#include <rpc++/urlparser.h>
#include <gtest/gtest.h>

using namespace oncrpc;
using namespace std;
using namespace testing;

struct UrlParserTest: public ::testing::Test
{
    UrlParserTest()
    {
    }
};

TEST_F(UrlParserTest, Hostbased)
{
    UrlParser p("tcp://server:1234/some/path");
    EXPECT_TRUE(p.isHostbased());
    EXPECT_EQ("tcp", p.scheme);
    EXPECT_EQ("server", p.host);
    EXPECT_EQ("1234", p.port);
    EXPECT_EQ("/some/path", p.path);
}

TEST_F(UrlParserTest, Pathbased)
{
    UrlParser p("file://some/path");
    EXPECT_TRUE(p.isPathbased());
    EXPECT_EQ("file", p.scheme);
    EXPECT_EQ("some/path", p.path);
    EXPECT_EQ(2, p.segments.size());

    UrlParser p2("file:///some/path");
    EXPECT_EQ("/some/path", p2.path);
    EXPECT_EQ(2, p2.segments.size());

    UrlParser p3("file:///");
    EXPECT_EQ("/", p3.path);
    EXPECT_EQ(0, p3.segments.size());
}

TEST_F(UrlParserTest, Query)
{
    UrlParser p("file://some/path?foo=bar&bar=baz");
    EXPECT_EQ("some/path", p.path);
    EXPECT_EQ("bar", p.query.find("foo")->second);
    EXPECT_EQ("baz", p.query.find("bar")->second);

    UrlParser p2("file://some/path?foo=bar;bar=baz");
    EXPECT_EQ("some/path", p2.path);
    EXPECT_EQ("bar", p2.query.find("foo")->second);
    EXPECT_EQ("baz", p2.query.find("bar")->second);

    UrlParser p3("tcp://host?foo=bar");
    EXPECT_EQ("host", p3.host);
    EXPECT_EQ("", p3.path);
    EXPECT_EQ("bar", p3.query.find("foo")->second);

    UrlParser p4("tcp://host?foo=1&foo=2&foo=3");
    auto range = p4.query.equal_range("foo");
    auto i = range.first;
    EXPECT_EQ("1", i->second); ++i;
    EXPECT_EQ("2", i->second); ++i;
    EXPECT_EQ("3", i->second); ++i;
}
