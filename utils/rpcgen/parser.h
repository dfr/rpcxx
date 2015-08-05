// -*- c++ -*-

#pragma once

#include <cassert>
#include <exception>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "lexer.h"
#include "types.h"
#include "utils.h"

namespace oncrpc {
namespace rpcgen {

using namespace ::std;

class SyntaxError: public runtime_error
{
public:
    SyntaxError(const Location& loc, const string& message);
};

class LookupError: public runtime_error
{
public:
    LookupError(const Location& loc, const string& name);
};

class Definition
{
public:
    virtual ~Definition() {}
    virtual void print(Indent indent, ostream& str) = 0;
    virtual int operator==(const Definition& other) const = 0;
    int operator!=(const Definition& other) const
    {
        return !(*this == other);
    }
};

class TypeDefinition: public Definition
{
public:
    TypeDefinition(Declaration&& decl)
        : decl_(move(decl))
    {
    }

    void print(Indent indent, ostream& str) override
    {
        str << indent
            << "TypeDefinition(" << decl_.first << ") { " << endl;
        decl_.second->print(indent + 1, str);
        str << indent << "}" << endl;
    }

    int operator==(const Definition& other) const override
    {
        auto p = dynamic_cast<const TypeDefinition*>(&other);
        if (!p)
            return false;
        return decl_.first == p->decl_.first &&
            *decl_.second == *p->decl_.second;
    }

    const string& name() const { return decl_.first; }
    shared_ptr<Type> type() const { return decl_.second; }

private:
    Declaration decl_;
};

class EnumDefinition: public Definition
{
public:
    EnumDefinition(
        const string& name, shared_ptr<EnumType>&& body)
        : name_(name),
          body_(move(body))
    {
    }

    void print(Indent indent, ostream& str) override
    {
        str << indent
            << "enum " << name_ << ": uint32_t {" << endl;
        body_->printFields(indent + 1, str);
        str << indent << "};" << endl;
    }

    int operator==(const Definition& other) const override
    {
        auto p = dynamic_cast<const EnumDefinition*>(&other);
        if (!p)
            return false;
        if (name_ != p->name_)
            return false;
        return *body_ == *p->body_;
    }

    const string& name() const { return name_; }
    shared_ptr<EnumType> body() const { return body_; }

private:
    string name_;
    shared_ptr<EnumType> body_;
};

class StructDefinition: public Definition
{
public:
    StructDefinition(
        const string& name, shared_ptr<StructType>&& body)
        : name_(name),
          body_(move(body))
    {
    }

    void print(Indent indent, ostream& str)
    {
        str << indent << "struct " << name_ << " {" << endl;
        body_->printFields(indent + 1, str);
        str << indent << "};" << endl;
    }

    int operator==(const Definition& other) const override
    {
        auto p = dynamic_cast<const StructDefinition*>(&other);
        if (!p)
            return false;
        if (name_ != p->name_)
            return false;
        return *body_ == *p->body_;
    }

    const string& name() const { return name_; }
    shared_ptr<StructType> body() const { return body_; }

private:
    string name_;
    shared_ptr<StructType> body_;
};

class UnionDefinition: public Definition
{
public:
    UnionDefinition(
        const string& name, shared_ptr<UnionType>&& body)
        : name_(name),
          body_(move(body))
    {
    }

    void print(Indent indent, ostream& str) override
    {
        str << indent
            << "struct " << name_ << " {" << endl;
        ++indent;
        body_->printFields(indent, name_, str);
        --indent;
        str << indent << "};" << endl;
    }

    int operator==(const Definition& other) const override
    {
        auto p = dynamic_cast<const UnionDefinition*>(&other);
        if (!p)
            return false;
        if (name_ != p->name_)
            return false;
        return *body_ == *p->body_;
    }

    const string& name() const { return name_; }
    shared_ptr<UnionType> body() const { return body_; }

private:
    string name_;
    shared_ptr<UnionType> body_;
};

class ConstantDefinition: public Definition
{
public:
    ConstantDefinition(const string& name, int value)
        : name_(name),
          value_(value)
    {
    }

    void print(Indent indent, ostream& str) override
    {
        str << indent
            << "constexpr int " << name_ << " = " << value_ << ";" << endl;
    }

    int operator==(const Definition& other) const override
    {
        auto p = dynamic_cast<const ConstantDefinition*>(&other);
        if (!p)
            return false;
        return name_ == p->name_ && value_ == p->value_;
    }

    const string& name() const { return name_; }
    int value() const { return value_; }

private:
    string name_;
    int value_;
};

class Procedure
{
public:
    Procedure(const string& name, int proc,
              shared_ptr<Type>&& retType,
              vector<shared_ptr<Type>>&& argTypes)
        : name_(name),
          proc_(proc),
          retType_(move(retType)),
          argTypes_(move(argTypes))
    {
    }

    void print(Indent indent, ostream& str) const;

    string methodName(int namePrefixLen)
    {
        return formatIdentifier(
            LCAMEL, parseIdentifier(name_.substr(namePrefixLen)));
    }

    void printDeclaration(
        Indent indent,
        int namePrefixLen,
        const string& methodPrefix,
        const string& methodSuffix,
        ostream& str);

    void printClientBody(Indent indent, ostream& str) const;

    vector<shared_ptr<Type>>::const_iterator
    begin() const { return argTypes_.begin(); }

    vector<shared_ptr<Type>>::const_iterator
    end() const { return argTypes_.end(); }

    string name() const { return name_; }
    int proc() const { return proc_; }
    shared_ptr<Type> retType() const { return retType_; }

private:
    string name_;
    int proc_;
    shared_ptr<Type> retType_;
    vector<shared_ptr<Type>> argTypes_;
};

class ProgramDefinition;

class ProgramVersion
{
public:
    ProgramVersion(const string& name, int vers = 0)
        : name_(name),
          vers_(vers)
    {
    }

    void print(Indent indent, ostream& str) const;

    void printInterface(
        Indent indent, ProgramDefinition* def, ostream& str) const;

    void printClientStubs(
        Indent indent, ProgramDefinition* def, ostream& str) const;

    void printServerStubs(
        Indent indent, ProgramDefinition* def, ostream& str) const;

    int operator==(const ProgramVersion& other) const
    {
        return name_ == other.name_;
    }

    int operator!=(const ProgramVersion& other) const
    {
        return !(*this == other);
    }

    void add(shared_ptr<Procedure>&& proc)
    {
        procs_.emplace_back(move(proc));
    }

    vector<shared_ptr<Procedure>>::const_iterator
    begin() const { return procs_.begin(); }

    vector<shared_ptr<Procedure>>::const_iterator
    end() const { return procs_.end(); }

    const string& name() const { return name_; }
    int vers() const { return vers_; }
    void setVers(int vers) { vers_ = vers; }

private:
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

    string name_;
    int vers_;
    vector<shared_ptr<Procedure>> procs_;
};

class ProgramDefinition: public Definition
{
public:
    ProgramDefinition(const string& name, int prog = 0)
        : name_(name),
          prog_(prog)
    {
    }

    template <typename... Args>
    ProgramDefinition(const string& name, int prog, Args... args)
        : name_(name),
          prog_(prog)
    {
        add(move(args)...);
    }

    void print(Indent indent, ostream& str) override
    {
        str << indent << "//program " << name_ << " {" << endl;
        ++indent;
        for (const auto& ver: versions_)
            ver->print(indent, str);
        --indent;
        str << indent << "//} = " << prog_ << ";" << endl;
    }

    int operator==(const Definition& other) const override
    {
        auto p = dynamic_cast<const ProgramDefinition*>(&other);
        if (!p)
            return false;
        if (name_ != p->name_ || prog_ != p->prog_)
            return false;
        if (versions_.size() != p->versions_.size())
            return false;
        for (auto i = versions_.begin(), j = p->versions_.begin();
             i != versions_.end(); ++i, ++j)
            if (*i->get() != *j->get())
                return false;
        return true;
    }

    template <typename... Rest>
    void add(shared_ptr<ProgramVersion>&& ver, Rest... rest)
    {
        add(move(ver));
        add(move(rest)...);
    }

    void add(shared_ptr<ProgramVersion>&& ver)
    {
        versions_.emplace_back(move(ver));
    }

    vector<shared_ptr<ProgramVersion>>::const_iterator
    begin() const { return versions_.begin(); }

    vector<shared_ptr<ProgramVersion>>::const_iterator
    end() const { return versions_.end(); }

    const string& name() const { return name_; }
    int prog() const { return prog_; }
    void setProg(int prog) { prog_ = prog; }

private:
    string name_;
    int prog_;
    vector<shared_ptr<ProgramVersion>> versions_;
};

class Visitor
{
public:
    virtual ~Visitor() {}
    virtual void visit(TypeDefinition* def) {}
    virtual void visit(EnumDefinition* def) {}
    virtual void visit(StructDefinition* def) {}
    virtual void visit(UnionDefinition* def) {}
    virtual void visit(ConstantDefinition* def) {}
    virtual void visit(ProgramDefinition* def) {}
};

class Specification
{
public:
    Specification()
    {
    }

    template <typename... Args>
    Specification(Args... args)
    {
        add(move(args)...);
    }

    void visit(Visitor* visitor)
    {
        for (const auto& def: definitions_) {
            {
                auto p = dynamic_cast<TypeDefinition*>(def.get());
                if (p) {
                    visitor->visit(p);
                    continue;
                }
            }
            {
                auto p = dynamic_cast<EnumDefinition*>(def.get());
                if (p) {
                    visitor->visit(p);
                    continue;
                }
            }
            {
                auto p = dynamic_cast<StructDefinition*>(def.get());
                if (p) {
                    visitor->visit(p);
                    continue;
                }
            }
            {
                auto p = dynamic_cast<UnionDefinition*>(def.get());
                if (p) {
                    visitor->visit(p);
                    continue;
                }
            }
            {
                auto p = dynamic_cast<ConstantDefinition*>(def.get());
                if (p) {
                    visitor->visit(p);
                    continue;
                }
            }
            {
                auto p = dynamic_cast<ProgramDefinition*>(def.get());
                if (p) {
                    visitor->visit(p);
                    continue;
                }
            }
        }
    }

    void print(Indent indent, ostream& str) const
    {
        str << indent << "Specification {" << endl;
        for (const auto& defn: definitions_)
            defn->print(indent + 1, str);
        str << indent << "}" << endl;
    }

    template <typename... Rest>
    void add(shared_ptr<Definition>&& defn, Rest... rest)
    {
        add(move(defn));
        add(move(rest)...);
    }

    void add(shared_ptr<Definition>&& defn)
    {
        definitions_.emplace_back(move(defn));
    }

    int operator==(const Specification& other) const
    {
        if (definitions_.size() != other.definitions_.size())
            return false;
        for (auto i = definitions_.begin(), j = other.definitions_.begin();
             i != definitions_.end(); ++i, ++j)
            if (*i->get() != *j->get())
                return false;
        return true;
    }

private:
    vector<shared_ptr<Definition>> definitions_;
};

static inline ostream& operator<<(ostream& str, const Specification& obj)
{
    obj.print(Indent(), str);
    return str;
}

class Parser
{
public:
    Parser(const std::string& file, istream& in, ostream& out);
    Parser(istream& in, ostream& out);

    static shared_ptr<Type> intType(int width, bool isSigned);
    static shared_ptr<Type> floatType(int width);
    static shared_ptr<Type> boolType();
    static shared_ptr<Type> voidType();

    shared_ptr<Specification> parse();
    shared_ptr<ConstantDefinition> parseConstantDefinition();
    shared_ptr<Value> parseValue();
    shared_ptr<EnumDefinition> parseEnumDefinition();
    shared_ptr<EnumType> parseEnumBody();
    shared_ptr<StructDefinition> parseStructDefinition();
    shared_ptr<StructType> parseStructBody();
    shared_ptr<UnionDefinition> parseUnionDefinition();
    shared_ptr<UnionType> parseUnionBody();
    shared_ptr<Type> parseTypeSpecifier();
    pair<string, shared_ptr<Type>> parseDeclaration();
    shared_ptr<ProgramDefinition> parseProgramDefinition();

private:
    void nextToken();
    void expectToken(int type);
    [[noreturn]] void unexpected();

    Lexer lexer_;
    Token tok_;

    static unordered_map<int, shared_ptr<Type>> signedIntTypes_;
    static unordered_map<int, shared_ptr<Type>> unsignedIntTypes_;
    static unordered_map<int, shared_ptr<Type>> floatTypes_;
    static shared_ptr<Type> boolType_;
    static shared_ptr<Type> voidType_;
};

}
}
