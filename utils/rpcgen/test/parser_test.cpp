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

#include "utils/rpcgen/parser.h"

using namespace oncrpc::rpcgen;
using namespace std;

namespace {

class ParserTest: public ::testing::Test
{
public:
};

TEST_F(ParserTest, SyntaxError)
{
    istringstream str("foo");
    EXPECT_THROW(Parser(str, cerr).parse(), SyntaxError);
}

TEST_F(ParserTest, ConstantDefinition)
{
    istringstream str("const foo = 123; const bar = 0xff;");
    EXPECT_EQ(
        Specification(
            make_shared<ConstantDefinition>("foo", 123),
            make_shared<ConstantDefinition>("bar", 255)),
        *Parser(str, cerr).parse().get());
}

TEST_F(ParserTest, TypeDefinition)
{
    istringstream str("typedef int foo;");
    EXPECT_EQ(
        Specification(
            make_shared<TypeDefinition>(
                make_pair(
                    "foo",
                    Parser::intType(32, true)))),
        *Parser(str, cerr).parse().get());
}

TEST_F(ParserTest, EnumDefinition)
{
    istringstream str("enum foo { bar = 1, baz = 2 };");
    EXPECT_EQ(
        Specification(
            make_shared<EnumDefinition>(
                "foo",
                make_shared<EnumType>(
                    make_pair("bar", make_shared<ConstantValue>(1)),
                    make_pair("baz", make_shared<ConstantValue>(2))))),
        *Parser(str, cerr).parse().get());
}

TEST_F(ParserTest, StructDefinition)
{
    istringstream str(
        "struct foo { int bar; unsigned int baz[2]; int qux<>; };");
    EXPECT_EQ(
        Specification(
            make_shared<StructDefinition>(
                "foo",
                make_shared<StructType>(
                    make_pair(
                        "bar",
                        Parser::intType(32, true)),
                    make_pair(
                        "baz",
                        make_shared<ArrayType>(
                            Parser::intType(32, false),
                            make_shared<ConstantValue>(2),
                            true)),
                    make_pair(
                        "qux",
                        make_shared<ArrayType>(
                            Parser::intType(32, true),
                            nullptr,
                            false))))),
        *Parser(str, cerr).parse().get());
}

TEST_F(ParserTest, UnionDefinition)
{
    istringstream str(
        R"decl(
        union foo switch (int i) {
            case 0:
            case 1: int foo;
            case x: hyper bar;
            default: int baz;
        };)decl");

    EXPECT_EQ(
        Specification(
            make_shared<UnionDefinition>(
                "foo",
                make_shared<UnionType>(
                    make_pair(
                        "i", Parser::intType(32, true)),
                    UnionArm(
                        ValueList(make_shared<ConstantValue>(0),
                                  make_shared<ConstantValue>(1)),
                        make_pair(
                            "foo", Parser::intType(32, true))),
                    UnionArm(
                        ValueList(make_shared<VariableValue>("x")),
                        make_pair(
                            "bar", Parser::intType(64, true))),
                    UnionArm(
                        ValueList{},
                        make_pair(
                            "baz",
                            Parser::intType(32, true)))))),
        *Parser(str, cerr).parse().get());
}

TEST_F(ParserTest, ForwardDecl)
{
    istringstream str("typedef struct foo bar;");
    EXPECT_EQ(
        Specification(
            make_shared<TypeDefinition>(
                make_pair(
                    "bar",
                    make_shared<NamedType>("foo")
                )
            )
        ),
        *Parser(str, cerr).parse().get()
    );
}

TEST_F(ParserTest, ProgramDefinition)
{
    istringstream str(
        "program foo { version bar { void null(void) = 0; } = 1; } = 1234;");
    EXPECT_EQ(
        Specification(
            make_shared<ProgramDefinition>(
                "foo", 1234,
                make_shared<ProgramVersion>("bar", 1))),
        *Parser(str, cerr).parse().get());
}

TEST_F(ParserTest, BasicTypes)
{
    istringstream str(
        R"decl(
        struct foo {
            int a;
            unsigned int b;
            hyper c;
            unsigned hyper d;
            float e;
            double f;
            quadruple g;
            bool h;
        };)decl");

    EXPECT_EQ(
        Specification(
            make_shared<StructDefinition>(
                "foo",
                make_shared<StructType>(
                    make_pair("a", Parser::intType(32, true)),
                    make_pair("b", Parser::intType(32, false)),
                    make_pair("c", Parser::intType(64, true)),
                    make_pair("d", Parser::intType(64, false)),
                    make_pair("e", Parser::floatType(32)),
                    make_pair("f", Parser::floatType(64)),
                    make_pair("g", Parser::floatType(128)),
                    make_pair("h", Parser::boolType())))),
        *Parser(str, cerr).parse().get());
}

}
