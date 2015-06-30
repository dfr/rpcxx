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
