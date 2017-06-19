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

#include <gtest/gtest.h>

#include "utils/rpcgen/lexer.h"

using namespace oncrpc::rpcgen;
using namespace std;

namespace {

class LexerTest: public ::testing::Test
{
public:
};

TEST_F(LexerTest, Comment)
{
    istringstream str("/* ignore */");
    Lexer lex(str, cerr);
    EXPECT_EQ(Token::END_OF_FILE, lex.nextToken().type());
}

TEST_F(LexerTest, Passthrough)
{
    istringstream str("%foo\n%bar");
    ostringstream out;
    Lexer lex(str, out);
    lex.nextToken();
    EXPECT_EQ("foo\nbar", out.str());
}

TEST_F(LexerTest, Whitespace)
{
    istringstream str(" \t\n\n");
    Lexer lex(str, cerr);
    EXPECT_EQ(Token::END_OF_FILE, lex.nextToken().type());
}

TEST_F(LexerTest, Identifiers)
{
    istringstream str("foo bar foo123 foo_");
    string values[] = {"foo", "bar", "foo123", "foo_"};
    Lexer lex(str, cerr);
    auto i = begin(values);
    for (;;) {
        auto tok = lex.nextToken();
        if (tok.type() == Token::END_OF_FILE)
            break;
        EXPECT_EQ(Token::IDENTIFIER, tok.type());
        EXPECT_EQ(*i, tok.svalue());
        ++i;
    }
    EXPECT_EQ(end(values), i);
}

TEST_F(LexerTest, Integer)
{
    // We are choosing to ignore octal integer constants
    istringstream str("0 1 2 3 -1 -2 0x100 0xaa -0xff 0xabcdef 0xABCDEF");
    int values[] = {0, 1, 2, 3, -1, -2, 0x100, 170, -255, 0xabcdef, 0xabcdef};
    Lexer lex(str, cerr);
    auto i = begin(values);;
    for (;;) {
        auto tok = lex.nextToken();
        if (tok.type() == Token::END_OF_FILE)
            break;
        EXPECT_EQ(Token::INTEGER, tok.type());
        EXPECT_EQ(*i, tok.ivalue());
        ++i;
    }
    EXPECT_EQ(end(values), i);
}

}
