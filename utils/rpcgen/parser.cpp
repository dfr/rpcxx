#include <cassert>
#include <cctype>
#include <sstream>

#include "parser.h"

using namespace oncrpc::rpcgen;
using namespace std;

unordered_map<int, shared_ptr<Type>> Parser::signedIntTypes_;
unordered_map<int, shared_ptr<Type>> Parser::unsignedIntTypes_;
unordered_map<int, shared_ptr<Type>> Parser::floatTypes_;
shared_ptr<Type> Parser::boolType_;
shared_ptr<Type> Parser::voidType_;

SyntaxError::SyntaxError(
    const Location& loc, const string& message)
    : runtime_error(
        [=]() {
            ostringstream msg;
            msg << loc << ": " << message;
            return msg.str();
        }())
{
}

LookupError::LookupError(const Location& loc, const string& name)
    : runtime_error(
        [=]() {
            ostringstream msg;
            msg << loc << ": " << name << " not found";
            return msg.str();
        }())
{
}

Parser::Parser(const string& file, istream& in, ostream& out)
    : lexer_(file, in, out),
      tok_(Location(), Token::END_OF_FILE)
{
    nextToken();
}

Parser::Parser(istream& in, ostream& out)
    : lexer_(in, out),
      tok_(Location(), Token::END_OF_FILE)
{
    nextToken();
}

shared_ptr<Type>
Parser::intType(int width, bool isSigned)
{
    auto& map = isSigned ? signedIntTypes_ : unsignedIntTypes_;
    auto i = map.find(width);
    if (i != map.end())
        return i->second;
    auto ty = make_shared<IntType>(width, isSigned);
    map.insert(make_pair(width, ty));
    return ty;
}

shared_ptr<Type>
Parser::floatType(int width)
{
    auto& map = floatTypes_;
    auto i = map.find(width);
    if (i != map.end())
        return i->second;
    auto ty = make_shared<FloatType>(width);
    map.insert(make_pair(width, ty));
    return ty;
}

shared_ptr<Type>
Parser::boolType()
{
    if (!boolType_)
        boolType_ = make_shared<BoolType>();
    return boolType_;
}

shared_ptr<Type>
Parser::voidType()
{
    if (!voidType_)
        voidType_ = make_shared<VoidType>();
    return voidType_;
}

shared_ptr<Specification>
Parser::parse()
{
    auto spec = make_shared<Specification>();

    while (tok_.type() != Token::END_OF_FILE) {
        switch (tok_.type()) {
        case Token::KCONST:
            spec->add(parseConstantDefinition());
            break;
        case Token::KTYPEDEF:
            nextToken();
            spec->add(make_shared<TypeDefinition>(parseDeclaration()));
            expectToken(';');
            break;
        case Token::KENUM:
            spec->add(parseEnumDefinition());
            break;
        case Token::KSTRUCT:
            spec->add(parseStructDefinition());
            break;
        case Token::KUNION:
            spec->add(parseUnionDefinition());
            break;
        case Token::KPROGRAM:
            spec->add(parseProgramDefinition());
            break;
        default:
            unexpected();
        }
    }

    return spec;
}

shared_ptr<ConstantDefinition>
Parser::parseConstantDefinition()
{
    nextToken();
    string name = tok_.svalue();
    expectToken(Token::IDENTIFIER);
    expectToken('=');
    int value = tok_.ivalue();;
    expectToken(Token::INTEGER);
    expectToken(';');
    return make_shared<ConstantDefinition>(name, value);
}

shared_ptr<Value>
Parser::parseValue()
{
    if (tok_.type() == Token::IDENTIFIER) {
        auto name = tok_.svalue();
        nextToken();
        // Translate booleans to C++ keywords
        if (name == "TRUE")
            name = "true";
        else if (name == "FALSE")
            name = "false";
        return make_shared<VariableValue>(name);
    }
    else if (tok_.type() == Token::INTEGER) {
        auto value = tok_.ivalue();
        nextToken();
        return make_shared<ConstantValue>(value);
    }
    unexpected();
    return nullptr;
}

shared_ptr<EnumDefinition>
Parser::parseEnumDefinition()
{
    nextToken();
    auto name = tok_.svalue();
    expectToken(Token::IDENTIFIER);
    auto res = make_shared<EnumDefinition>(name, parseEnumBody());
    expectToken(';');
    return res;
}

shared_ptr<EnumType>
Parser::parseEnumBody()
{
    auto res = make_shared<EnumType>();
    expectToken('{');
    while (tok_.type() == Token::IDENTIFIER) {
        string name = tok_.svalue();
        nextToken();
        expectToken('=');
        auto value = parseValue();
        res->add(make_pair(move(name), move(value)));

        if (tok_.type() == ',')
            nextToken();
    }
    expectToken('}');
    return res;
}

shared_ptr<StructDefinition>
Parser::parseStructDefinition()
{
    nextToken();
    auto name = tok_.svalue();
    expectToken(Token::IDENTIFIER);
    auto res = make_shared<StructDefinition>(name, parseStructBody());
    expectToken(';');
    return res;
}

shared_ptr<StructType>
Parser::parseStructBody()
{
    auto res = make_shared<StructType>();
    expectToken('{');
    do {
        // If the next token can start a declaration, parse it
        switch (tok_.type()) {
        case Token::KOPAQUE:
        case Token::KOPAQUEREF:
        case Token::KSTRING:
        case Token::KVOID:
        case Token::KUNSIGNED:
        case Token::KINT:
        case Token::KHYPER:
        case Token::KFLOAT:
        case Token::KDOUBLE:
        case Token::KQUADRUPLE:
        case Token::KBOOL:
        case Token::KENUM:
        case Token::KSTRUCT:
        case Token::KUNION:
        case Token::IDENTIFIER:
            res->add(move(parseDeclaration()));
            expectToken(';');
            break;
        default:
            unexpected();
        }
    } while (tok_.type() != '}');
    nextToken();
    return move(res);
}

shared_ptr<UnionDefinition>
Parser::parseUnionDefinition()
{
    nextToken();
    auto name = tok_.svalue();
    expectToken(Token::IDENTIFIER);
    auto res = make_shared<UnionDefinition>(name, parseUnionBody());
    expectToken(';');
    return res;
}

shared_ptr<UnionType>
Parser::parseUnionBody()
{
    expectToken(Token::KSWITCH);
    expectToken('(');
    auto res = make_shared<UnionType>(parseDeclaration());
    expectToken(')');
    expectToken('{');
    do {
        ValueList values;
        do {
            expectToken(Token::KCASE);
            values.add(parseValue());
            expectToken(':');
        } while (tok_.type() == Token::KCASE);
        res->add(UnionArm(move(values), parseDeclaration()));
        expectToken(';');
    } while (tok_.type() == Token::KCASE);
    if (tok_.type() == Token::KDEFAULT) {
        nextToken();
        expectToken(':');
        res->add(UnionArm(ValueList(), parseDeclaration()));
        expectToken(';');
    }
    expectToken('}');
    return res;
}

shared_ptr<Type>
Parser::parseTypeSpecifier()
{
    switch (tok_.type()) {
    case Token::KVOID:
        nextToken();
        return voidType();
        break;

    case Token::KUNSIGNED:
        nextToken();
        if (tok_.type() == Token::KINT) {
            nextToken();
            return intType(32, false);
        }
        else if (tok_.type() == Token::KHYPER) {
            nextToken();
            return intType(64, false);
        }
        return intType(32, false);

    case Token::KINT:
        nextToken();
        return intType(32, true);

    case Token::KHYPER:
        nextToken();
        return intType(64, true);

    case Token::KFLOAT:
        nextToken();
        return floatType(32);

    case Token::KDOUBLE:
        nextToken();
        return floatType(64);

    case Token::KQUADRUPLE:
        nextToken();
        return floatType(128);

    case Token::KBOOL:
        nextToken();
        return boolType();

    case Token::KENUM:
        nextToken();
        return parseEnumBody();

    case Token::KSTRUCT:
        nextToken();
        if (tok_.type() == Token::IDENTIFIER) {
            auto name = tok_.svalue();
            nextToken();
            return make_shared<NamedStructType>(name);
        }
        return parseStructBody();

    case Token::KUNION:
        nextToken();
        if (tok_.type() == Token::IDENTIFIER) {
            auto name = tok_.svalue();
            nextToken();
            return make_shared<NamedUnionType>(name);
        }
        return parseUnionBody();

    case Token::IDENTIFIER: {
        auto name = tok_.svalue();
        nextToken();
        return make_shared<NamedType>(name);
    }
    }
    unexpected();
}

pair<string, shared_ptr<Type>>
Parser::parseDeclaration()
{
    if (tok_.type() == Token::KOPAQUE) {
        nextToken();
        auto name = tok_.svalue();
        expectToken(Token::IDENTIFIER);
        if (tok_.type() == '[') {
            nextToken();
            auto value = parseValue();
            expectToken(']');
            return make_pair(
                move(name),
                make_shared<OpaqueType>(move(value), true));
        }
        if (tok_.type() == '<') {
            nextToken();
            if (tok_.type() == '>') {
                nextToken();
                return make_pair(move(name), make_shared<OpaqueType>());
            }
            auto value = parseValue();
            expectToken('>');
            return make_pair(
                move(name),
                make_shared<OpaqueType>(move(value), false));
        }
        unexpected();
    }
    if (tok_.type() == Token::KOPAQUEREF) {
        nextToken();
        auto name = tok_.svalue();
        expectToken(Token::IDENTIFIER);
        if (tok_.type() == '<') {
            nextToken();
            if (tok_.type() == '>') {
                nextToken();
                return make_pair(move(name), make_shared<OpaqueRefType>());
            }
            auto value = parseValue();
            expectToken('>');
            return make_pair(
                move(name),
                make_shared<OpaqueRefType>(move(value)));
        }
        unexpected();
    }
    if (tok_.type() == Token::KSTRING) {
        nextToken();
        auto name = tok_.svalue();
        expectToken(Token::IDENTIFIER);
        if (tok_.type() == '<') {
            nextToken();
            if (tok_.type() == '>') {
                nextToken();
                return make_pair(move(name), make_shared<StringType>());
            }
            auto value = parseValue();
            expectToken('>');
            return make_pair(
                move(name),
                make_shared<StringType>(move(value)));
        }
        unexpected();
    }
    if (tok_.type() == Token::KVOID) {
        nextToken();
        return make_pair("", voidType());
    }
    auto type = parseTypeSpecifier();
    if (tok_.type() == '*') {
        nextToken();
        auto name = tok_.svalue();
        expectToken(Token::IDENTIFIER);
        return make_pair(move(name), make_shared<PointerType>(move(type)));
    }
    auto name = tok_.svalue();
    expectToken(Token::IDENTIFIER);
    if (tok_.type() == '[') {
        nextToken();
        auto size = parseValue();
        expectToken(']');
        type = make_shared<ArrayType>(move(type), move(size), true);
    }
    if (tok_.type() == '<') {
        nextToken();
        if (tok_.type() == '>') {
            type = make_shared<ArrayType>(move(type), nullptr, false);
        }
        else {
            auto size = parseValue();
            type = make_shared<ArrayType>(move(type), move(size), false);
        }
        expectToken('>');
    }
    return make_pair(move(name), move(type));
}

shared_ptr<ProgramDefinition>
Parser::parseProgramDefinition()
{
    nextToken();
    auto name = tok_.svalue();
    expectToken(Token::IDENTIFIER);
    expectToken('{');
    auto res = make_shared<ProgramDefinition>(name);
    do {
        expectToken(Token::KVERSION);
        auto vername = tok_.svalue();
        expectToken(Token::IDENTIFIER);
        expectToken('{');
        auto ver = make_shared<ProgramVersion>(vername);
        do {
            auto retType = parseTypeSpecifier();
            vector<shared_ptr<Type>> argTypes;
            auto procname = tok_.svalue();
            expectToken(Token::IDENTIFIER);
            expectToken('(');
            for (;;) {
                argTypes.push_back(parseTypeSpecifier());
                if (tok_.type() == ')')
                    break;
                expectToken(',');
            }
            expectToken(')');
            expectToken('=');
            int proc = tok_.ivalue();
            expectToken(Token::INTEGER);
            expectToken(';');
            ver->add(
                make_shared<Procedure>(
                    procname, proc, move(retType), move(argTypes)));
        } while (tok_.type() != '}');
        expectToken('}');
        expectToken('=');
        ver->setVers(tok_.ivalue());
        expectToken(Token::INTEGER);
        expectToken(';');
        res->add(move(ver));
    } while (tok_.type() == Token::KVERSION);
    expectToken('}');
    expectToken('=');
    res->setProg(tok_.ivalue());
    expectToken(Token::INTEGER);
    expectToken(';');

    return res;
}

void
Parser::nextToken()
{
    tok_ = lexer_.nextToken();
}

void
Parser::expectToken(int type)
{
    if (tok_.type() != type) {
        ostringstream ss;
        ss << "expected " << Token::to_string(type)
           << ", not " << Token::to_string(tok_.type());
        throw SyntaxError(tok_.loc(), ss.str());
    }
    nextToken();
}

void
Parser::unexpected()
{
    ostringstream ss;
    ss << "unexpected " << Token::to_string(tok_.type());
    throw SyntaxError(tok_.loc(), ss.str());
}
