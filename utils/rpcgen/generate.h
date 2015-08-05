// -*- c++ -*-

#pragma once

#include <iostream>

#include "parser.h"
#include "utils.h"

namespace oncrpc {
namespace rpcgen {

using namespace ::std;

class GenerateBase: public Visitor
{
public:
    GenerateBase(ostream& str)
        : str_(str)
    {
    }

protected:
    ostream& str_;
};

class GenerateTypes: public GenerateBase
{
public:
    GenerateTypes(ostream& str)
        : GenerateBase(str)
    {
    }

    void visit(TypeDefinition* def) override
    {
        def->type()->forwardDeclarations(Indent(), str_);
        str_ << "typedef " << *def->type() << " " << def->name()
             << ";" << endl << endl;
    }

    void visit(EnumDefinition* def) override
    {
        def->print(Indent(), str_);
        str_ << endl;
    }

    void visit(StructDefinition* def) override
    {
        def->print(Indent(), str_);
        str_ << endl;
    }

    void visit(UnionDefinition* def) override
    {
        Indent indent;
        def->print(indent, str_);
        str_ << endl;
    }

    void visit(ConstantDefinition* def) override
    {
        def->print(Indent(), str_);
        str_ << endl;
    }
};

class GenerateXdr: public GenerateBase
{
public:
    GenerateXdr(ostream& str)
        : GenerateBase(str)
    {
    }

    void visit(EnumDefinition* def) override
    {
        str_ << "template <typename XDR>" << endl
             << "static inline void xdr("
             << "oncrpc::RefType<" << def->name()
             << ", XDR> v, XDR* xdrs)" << endl
             << "{" << endl
             << "    xdr(reinterpret_cast<oncrpc::RefType<std::uint32_t, XDR>>(v)"
             << ", xdrs);" << endl;
        str_ << "}" << endl << endl;
    }

    void visit(StructDefinition* def) override
    {
        str_ << "template <typename XDR>" << endl
             << "static inline void xdr("
             << "oncrpc::RefType<" << def->name()
             << ", XDR> v, XDR* xdrs)" << endl
             << "{" << endl;
        for (const auto& field: *def->body()) {
            str_ << "    xdr(v." << field.first << ", xdrs);" << endl;
        }
        str_ << "}" << endl << endl;
    }

    void visit(UnionDefinition* def) override
    {
        Indent indent;
        str_ << "static inline void xdr(const "
             << def->name() << "& v, oncrpc::XdrSink* xdrs)" << endl
             << "{" << endl;
        ++indent;
        str_ << indent << "xdr(v." << def->body()->discriminant().first
             << ", xdrs);" << endl;
        def->body()->printSwitch(
            indent, str_, "v.",
            [this](Indent indent, auto name, auto type)
            {
                if (name.size() == 0)
                    return;
                str_ << indent << "xdr(v." << name << "(), xdrs);" << endl;
            });
        --indent;
        str_ << "}" << endl << endl;

        str_ << "static inline void xdr("
             << def->name() << "& v, oncrpc::XdrSource* xdrs)" << endl
             << "{" << endl;
        ++indent;
        str_ << indent << "v._clear();" << endl;
        str_ << indent << "xdr(v." << def->body()->discriminant().first
             << ", xdrs);" << endl;
        str_ << indent << "v._setType(v."
             << def->body()->discriminant().first << ");" << endl;
        def->body()->printSwitch(
            indent, str_, "v.",
            [this](Indent indent, auto name, auto type)
            {
                if (name.size() == 0)
                    return;
                str_ << indent << "xdr(v." << name << "(), xdrs);" << endl;
            });
        --indent;
        str_ << "}" << endl << endl;
    }
};

class GenerateInterface: public GenerateBase
{
public:
    GenerateInterface(ostream& str)
        : GenerateBase(str)
    {
    }

    void visit(ProgramDefinition* def) override
    {
        str_ << "constexpr int " << def->name()
             << " = " << def->prog() << ";" << endl;
        str_ << endl;

        for (const auto& ver: *def) {
            ver->printInterface(Indent(), def, str_);
        }
    }
};

class GenerateClient: public GenerateBase
{
public:
    GenerateClient(ostream& str)
        : GenerateBase(str)
    {
    }

    void visit(ProgramDefinition* def) override
    {
        for (const auto& ver: *def) {
            ver->printClientStubs(Indent(), def, str_);
        }
    }
};

class GenerateServer: public GenerateBase
{
public:
    GenerateServer(ostream& str)
        : GenerateBase(str)
    {
    }

    void visit(ProgramDefinition* def) override
    {
        for (const auto& ver: *def) {
            ver->printServerStubs(Indent(), def, str_);
        }
    }
};

}
}
