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
