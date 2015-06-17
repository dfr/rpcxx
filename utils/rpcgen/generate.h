// -*- c++ -*-

#pragma once

#include <iostream>

#include "utils/rpcgen/parser.h"

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
	str_ << "constexpr int FALSE = false;" << endl;
	str_ << "constexpr int TRUE = true;" << endl;
	str_ << endl;
    }

    void visit(TypeDefinition* def) override
    {
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
	     << "static void xdr("
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
	     << "static void xdr("
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
	str_ << "static void xdr(const "
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

	str_ << "static void xdr("
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

/// Return the length of the longest common prefix
static int
longestCommonPrefix(const vector<string>& methods)
{
    if (methods.size() == 0)
	return 0;

    string prefix = methods[0];
    for (auto i = methods.begin() + 1; i != methods.end(); ++i) {
	const string& s = *i;
	while (prefix.size() > 0 && s.find(prefix) != 0) {
	    prefix = prefix.substr(0, prefix.size() - 1);
	}
    }
    return prefix.size();
}

static void
lcase(string& s)
{
    transform(s.begin(), s.end(), s.begin(),
	      [](char ch) {return std::tolower(ch);});

}

class GenerateClient: public GenerateBase
{
public:
    GenerateClient(ostream& str)
	: GenerateBase(str)
    {
    }

    virtual void visit(ProgramDefinition* def)
    {
	str_ << "constexpr int " << def->name()
	     << " = " << def->prog() << ";" << endl;
	str_ << endl;

	string baseClassName = def->name();
	lcase(baseClassName);

	for (const auto& ver: *def) {
	    str_ << "constexpr int " << ver->name()
		 << " = " << ver->vers() << ";" << endl;
	    str_ << endl;
	    vector<string> methods;
	    for (const auto& proc: *ver) {
		methods.push_back(proc->name());
		str_ << "constexpr int " << proc->name()
		     << " = " << proc->proc() << ";" << endl;
	    }
	    str_ << endl;
	    int prefixlen = longestCommonPrefix(methods);

	    string className = baseClassName + "_" + to_string(ver->vers());
	    str_ << "class " << className << " {" << endl;
	    Indent indent(1);
	    str_ << indent << className
		 << "(std::shared_ptr<oncrpc::Channel> channel)" << endl;
	    ++indent;
	    str_ << indent << ": channel_(channel)" << endl;
	    --indent;
	    str_ << indent << "{}" << endl;
	    str_ << "public:" << endl;
	    for (const auto& proc: *ver) {
		string methodName = proc->name().substr(prefixlen);
		lcase(methodName);
		str_ << indent << *proc->retType() << " " << methodName;
		str_ << "(";
		string sep = "";
		int i = 0;
		for (const auto& argType: *proc) {
		    if (argType == Parser::voidType())
			continue;
		    str_ << sep << "const " << *argType << "& _arg" << i;
		    sep = ", ";
		    i++;
		}
		str_ << ")" << endl;
		str_ << indent << "{" << endl;
		++indent;
		if (proc->retType() != Parser::voidType())
		    str_ << indent << *proc->retType() << " _ret;" << endl;
		str_ << indent << "channel_->call(" << endl;
		++indent;
		str_ << indent
		     << def->name() << ", "
		     << ver->name() << ", "
		     << proc->name() << "," << endl;
		str_ << indent << "[&](oncrpc::XdrSink* xdrs) {" << endl;
		++indent;
		i = 0;
		for (const auto& argType: *proc) {
		    if (argType == Parser::voidType())
			continue;
		    str_ << indent << "xdr(_arg" << i << ", xdrs);" << endl;
		    i++;
		}
		--indent;
		str_ << indent << "}," << endl;
		str_ << indent << "[&](oncrpc::XdrSource* xdrs) {" << endl;
		++indent;
		if (proc->retType() != Parser::voidType())
		    str_ << indent << "xdr(_res, xdrs);" << endl;
		--indent;
		str_ << indent << "});" << endl;
		--indent;
		if (proc->retType() != Parser::voidType())
		    str_ << indent << "return std::move(_res);" << endl;
		--indent;
		str_ << indent << "}" << endl;
	    }
	    str_ << "private:" << endl;
	    str_ << indent
		 << "std::shared_ptr<oncrpc::Channel> channel_;" << endl;
	    str_ << "};" << endl;
	    str_ << endl;
	}
    }
};

}
}
