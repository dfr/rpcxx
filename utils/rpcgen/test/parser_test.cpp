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
    istringstream str("struct foo { int bar; unsigned int baz[2]; };");
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
			    true))))),
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
		    make_pair(
			ValueList(move(make_shared<ConstantValue>(0)),
				  move(make_shared<ConstantValue>(1))),
			make_pair(
			    "foo", Parser::intType(32, true))),
		    make_pair(
			ValueList(make_shared<VariableValue>("x")),
			make_pair(
			    "bar", Parser::intType(64, true))),
		    make_pair(
			ValueList{},
			make_pair(
			    "baz",
			    Parser::intType(32, true)))))),
	*Parser(str, cerr).parse().get());
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
