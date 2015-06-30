// -*- c++ -*-

#pragma once

#include <cassert>
#include <exception>
#include <istream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "utils/rpcgen/lexer.h"

namespace oncrpc {
namespace rpcgen {

using namespace ::std;

class Indent
{
public:
    Indent() : level_(0) {}
    explicit Indent(int level) : level_(level) {}
    Indent(const Indent& other) : level_(other.level_) {}
    Indent& operator++()
    {
        level_++;
        return *this;
    }
    Indent& operator--()
    {
        level_--;
        return *this;
    }
    Indent operator+(int delta)
    {
        return Indent(level_ + delta);
    }
    int level_;
};

static inline ostream& operator<<(ostream& str, const Indent& indent)
{
    str << string(4*indent.level_, ' ');
    return str;
}

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

class Value
{
public:
    virtual ~Value() {}
    virtual void print(Indent indent, ostream& str) const = 0;
    virtual int operator==(const Value& other) const = 0;
    int operator!=(const Value& other) const
    {
        return !(*this == other);
    }
};

static inline ostream& operator<<(ostream& str, const Value& obj)
{
    obj.print(Indent(), str);
    return str;
}

class ConstantValue: public Value
{
public:
    ConstantValue(int value)
        : value_(value)
    {
    }

    void print(Indent indent, ostream& str) const override
    {
        str << value_;
    }

    int operator==(const Value& other) const override
    {
        auto p = dynamic_cast<const ConstantValue*>(&other);
        if (p)
            return value_ == p->value_;
        return false;
    }

private:
    int value_;
};

class VariableValue: public Value
{
public:
    VariableValue(const string& name)
        : name_(name)
    {
    }

    void print(Indent indent, ostream& str) const override
    {
        str << name_;
    }

    int operator==(const Value& other) const override
    {
        auto p = dynamic_cast<const VariableValue*>(&other);
        if (p)
            return name_ == p->name_;
        return false;
    }

private:
    string name_;
};

class Type
{
public:
    virtual ~Type() {}
    virtual bool isPOD() const = 0;
    virtual void print(Indent indent, ostream& str) const = 0;

    virtual int operator==(const Type& other) const = 0;
    int operator!=(const Type& other) const
    {
        return !(*this == other);
    }
};

static inline ostream& operator<<(ostream& str, const Type& obj)
{
    obj.print(Indent(), str);
    return str;
}

class NamedType: public Type
{
public:
    NamedType(const string& name)
        : name_(name)
    {
    }

    virtual bool isPOD() const
    {
        // ideally, we look up the type
        return false;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        str << name_;
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const NamedType*>(&other);
        if (p)
            return name_ == p->name_;
        return false;
    }

private:
    string name_;
};

class VoidType: public Type
{
public:
    VoidType()
    {
    }

    virtual bool isPOD() const
    {
        return true;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        str << "void";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const VoidType*>(&other);
        if (p)
            return true;
        return false;
    }
};

class PointerType: public Type
{
public:
    PointerType(shared_ptr<Type>&& type)
        : type_(move(type))
    {
    }

    virtual bool isPOD() const
    {
        return false;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        str << "std::unique_ptr<" << *type_ << ">";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const PointerType*>(&other);
        if (p)
            return *type_ == *p->type_;
        return false;
    }
private:
    shared_ptr<Type> type_;
};

class IntType: public Type
{
public:
    IntType(int width, bool isSigned)
        : width_(width),
          isSigned_(isSigned)
    {
    }

    virtual bool isPOD() const
    {
        return true;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        str << "std::";
        if (!isSigned_)
            str << "u";
        str << "int" << width_ << "_t";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const IntType*>(&other);
        if (p)
            return width_ == p->width_ && isSigned_ == p->isSigned_;
        return false;
    }

private:
    int width_;
    bool isSigned_;
};

class FloatType: public Type
{
public:
    FloatType(int width)
        : width_(width)
    {
    }

    virtual bool isPOD() const
    {
        return true;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        switch (width_) {
        case 32:
            str << "float";
            break;
        case 64:
            str << "double";
            break;
        case 128:
            str << "long double";
            break;
        default:
            abort();
        }
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const FloatType*>(&other);
        if (p)
            return width_ == p->width_;
        return false;
    }

private:
    int width_;
};

class BoolType: public Type
{
public:
    BoolType()
    {
    }

    virtual bool isPOD() const
    {
        return true;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        str << "int /* bool */";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const BoolType*>(&other);
        if (p)
            return true;
        return false;
    }
};

class OpaqueType: public Type
{
public:
    OpaqueType()
        : isFixed_(false)
    {
    }

    OpaqueType(shared_ptr<Value>&& size, bool isFixed)
        : size_(move(size)),
          isFixed_(isFixed)
    {
    }

    virtual bool isPOD() const
    {
        return false;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        if (isFixed_)
            str << "std::array<std::uint8_t, " << *size_ << ">";
        else if (size_)
            str << "oncrpc::bounded_vector<std::uint8_t, " << *size_ << ">";
        else
            str << "std::vector<std::uint8_t>";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const OpaqueType*>(&other);
        if (p)
            return *size_.get() == *p->size_.get() && isFixed_ == p->isFixed_;
        return false;
    }

private:
    shared_ptr<Value> size_;
    bool isFixed_;
};

class StringType: public Type
{
public:
    StringType()
    {
    }

    StringType(shared_ptr<Value>&& size)
        : size_(move(size))
    {
    }

    virtual bool isPOD() const
    {
        return false;
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        if (size_)
            str << "oncrpc::bounded_string<" << *size_ << ">";
        else
            str << "std::string";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const StringType*>(&other);
        if (p)
            return *size_.get() == *p->size_.get();
        return false;
    }

private:
    shared_ptr<Value> size_;
};

class ArrayType: public Type
{
public:
    ArrayType(
        shared_ptr<Type>&& type, shared_ptr<Value>&& size,
        bool isFixed)
        : type_(move(type)),
          size_(move(size)),
          isFixed_(isFixed)
    {
    }

    virtual bool isPOD() const
    {
        return type_->isPOD();
    }

    virtual void print(Indent indent, ostream& str) const override
    {
        if (isFixed_)
            str << "std::array<" << *type_ << ", " << *size_ << ">";
        else
            str << "oncrpc::bounded_vector<" << *type_ << ", " << *size_ << ">";
    }

    virtual int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const ArrayType*>(&other);
        if (p)
            return *type_ == *p->type_ &&
                *size_ == *p->size_ &&
                isFixed_ == p->isFixed_;
        return false;
    }

private:
    shared_ptr<Type> type_;
    shared_ptr<Value> size_;
    bool isFixed_;
};

class EnumType: public Type
{
public:
    typedef pair<string, shared_ptr<Value>> fieldT;

    EnumType()
    {
    }

    template <typename... Args>
    EnumType(Args... args)
    {
        add(move(args)...);
    }

    virtual bool isPOD() const
    {
        return true;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << indent << "enum {" << endl;
        printFields(indent + 1, str);
        str << indent << "}" << endl;
    }

    void printFields(Indent indent, ostream& str) const
    {
        for (const auto& field: fields_)
            str << indent
                << field.first << " = " << *field.second.get()
                << "," << endl;
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const EnumType*>(&other);
        if (!p)
            return false;
        for (auto i = fields_.begin(), j = p->fields_.begin();
             i != fields_.end(); ++i, ++j) {
            if (i->first != j->first ||
                *i->second.get() != *j->second.get())
                return false;
        }
        return true;
    }

    template <typename... Rest>
    void add(fieldT&& field, Rest... rest)
    {
        add(move(field));
        add(move(rest)...);
    }

    void add(fieldT&& field)
    {
        fields_.emplace_back(move(field));
    }

    vector<fieldT>::const_iterator
    begin() const { return fields_.begin(); }

    vector<fieldT>::const_iterator
    end() const { return fields_.end(); }

private:
    vector<fieldT> fields_;
};

typedef pair<string, shared_ptr<Type>> Declaration;

class StructType: public Type
{
public:
    StructType()
    {
    }

    template <typename... Args>
    StructType(Args... args)
    {
        add(move(args)...);
    }

    virtual bool isPOD() const
    {
        for (const auto& field: fields_) {
            if (!field.second->isPOD())
                return false;
        }
        return true;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << indent << "struct {" << endl;
        printFields(indent + 1, str);
        str << indent << "}" << endl;
    }

    void printFields(Indent indent, ostream& str) const
    {
        for (const auto& field: fields_) {
            str << indent;
            field.second->print(indent + 4, str);
            str << " " << field.first << ";" << endl;
        } 
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const StructType*>(&other);
        if (!p)
            return false;
        for (auto i = fields_.begin(), j = p->fields_.begin();
             i != fields_.end(); ++i, ++j) {
            if (i->first != j->first)
                return false;
            if (*i->second.get() != *j->second.get())
                return false;
        }
        return true;
    }

    template <typename... Rest>
    void add(Declaration&& field, Rest... rest)
    {
        add(move(field));
        add(move(rest)...);
    }

    void add(Declaration&& field)
    {
        fields_.emplace_back(move(field));
    }

    vector<Declaration>::const_iterator
    begin() const { return fields_.begin(); }

    vector<Declaration>::const_iterator
    end() const { return fields_.end(); }

private:
    vector<Declaration> fields_;
};

class ValueList
{
public:
    ValueList()
    {
    }

    ValueList(ValueList&& other)
        : values_(move(other.values_))
    {
    }

    template <typename... Args>
    ValueList(Args... args)
    {
        add(move(args)...);
    }

    void add(shared_ptr<Value>&& v)
    {
        values_.emplace_back(move(v));
    }

    template <typename... Rest>
    void add(shared_ptr<Value>&& v, Rest... rest)
    {
        add(move(v));
        add(move(rest)...);
    }

    size_t size() const { return values_.size(); }

    vector<shared_ptr<Value>>::const_iterator
    begin() const { return values_.begin(); }

    vector<shared_ptr<Value>>::const_iterator
    end() const { return values_.end(); }

    int operator==(const ValueList& other) const
    {
        if (values_.size() != other.values_.size())
            return false;
        for (size_t i = 0; i < values_.size(); i++)
            if (*values_[i] != *other.values_[i])
                return false;
        return true;
    }

    int operator!=(const ValueList& other) const
    {
        return !(*this == other);
    }
    
private:
    vector<shared_ptr<Value>> values_;
};

typedef pair<ValueList, Declaration> UnionArm;

class UnionType: public Type
{
public:
    UnionType(Declaration&& discriminant)
        : discriminant_(move(discriminant))
    {
    }

    template <typename... Args>
    UnionType(Declaration&& discriminant, Args... args)
        : discriminant_(move(discriminant))
    {
        add(move(args)...);
    }

    virtual bool isPOD() const
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
        for (const auto& field: fields_) {
            // Throw an exception with line number etc
            assert(field.second.second->isPOD());
        }
        str << indent << "struct {" << endl;
        printFields(indent + 1, str);
        str << indent << "}" << endl;
    }

    void printFields(Indent indent, ostream& str) const
    {
        str << indent;
        discriminant_.second->print(indent, str);
        str << " " << discriminant_.first << ";" << endl
            << indent << "union _u {" << endl;
        ++indent;
        for (const auto& field: fields_) {
            if (field.first.size() > 0) {
                for (const auto& val: field.first) {
                    str << indent
                        << "// case " << *val << ":" << endl;
                }
            }
            else {
                str << indent
                    << "// default:" << endl;   
            }
            // If we have no name, its a void field so ignore it
            if (field.second.first.size() > 0) {
                str << indent;
                field.second.second->print(indent + 4, str);
                str << " " << field.second.first << ";" << endl;
            }
        }
        --indent;
        str << indent << "};" << endl;
        str << indent << "std::aligned_union<" << endl;
        ++indent;
        str << indent << "sizeof(_u)";
        for (const auto& field: fields_) {
            if (field.second.first.size() == 0)
                continue;
            str << "," << endl << indent;
            field.second.second->print(indent + 4, str);
        }
        --indent;
        str << "> _storage;" << endl;
        str << indent << "bool _hasValue;" << endl;

        printAccessors(indent, str, false);
        printAccessors(indent, str, true);

        // u._clear() calls active field destructor
        str << indent << "void _clear() {" << endl;
        ++indent;
        str << indent << "if (!_hasValue) return;" << endl;
        printSwitch(
            indent, str, "",
            [&str](Indent indent, auto name, auto type)
            {
                if (name.size() == 0)
                    return;
                str << indent << "reinterpret_cast<"
                    << *type << "*>(&_storage)->~"
                    << *type << "();" << endl;
            });
        str << indent << "_hasValue = false;" << endl;
        --indent;
        str << indent << "}" << endl;

        // u._setType(v) sets discriminant and calls constructor
        str << indent << "void _setType("
            << *discriminant_.second << " _v) {" << endl;
        ++indent;
        str << indent << "if (_hasValue) _clear();" << endl;
        printSwitch(
            indent, str, "",
            [&str](Indent indent, auto name, auto type)
            {
                if (name.size() == 0)
                    return;
                str << indent << "new(&_storage) "
                    << *type << "();" << endl;
            });
        str << indent << "_hasValue = true;" << endl;
        str << indent << discriminant_.first << " = _v;" << endl;
        --indent;
        str << indent << "}" << endl;
    }

    void printAccessors(Indent indent, ostream& str, bool isConst) const
    {
        string attr = "";
        if (isConst)
            attr = "const ";
        for (const auto& field: fields_) {
            if (field.second.first.size() == 0)
                continue;
            str << indent << attr;
            field.second.second->print(indent, str);
            str << "& " << field.second.first << "() " << attr << "{" << endl;
            ++indent;
            str << indent << "assert(_hasValue";
            if (field.first.size() > 0) {
                str << " && (";
                bool firstValue = true;
                for (const auto& val: field.first) {
                    if (firstValue) {
                        firstValue = false;
                    }
                    else {
                        str << " || ";
                    }
                    str << discriminant_.first << " == " << *val;
                }
                str << ")";
            }
            else {
                for (const auto& val: values_)
                    str << " && " << discriminant_.first << " != " << *val;
            }
            str << ");" << endl;
            str << indent << "return *reinterpret_cast<" << attr;
            field.second.second->print(indent, str);
            str << "*>(&_storage);" << endl;
            --indent;
            str << indent << "}" << endl;
        }
    }

    template <typename F>
    void printSwitch(
        Indent indent, ostream& str, const string& prefix, F&& fn) const
    {
        str << indent << "switch (" << prefix
            << discriminant_.first << ") {" << endl;
        for (const auto& field: fields_) {
            if (field.first.size() > 0) {
                for (const auto& val: field.first) {
                    str << indent << "case " << *val << ":" << endl;
                }
            }
            else {
                str << indent << "default:" << endl;
            }
            ++indent;
            fn(indent, field.second.first, field.second.second);
            str << indent << "break;" << endl;
            --indent;
        }
        str << indent << "}" << endl;
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const UnionType*>(&other);
        if (!p)
            return false;
        for (auto i = fields_.begin(), j = p->fields_.begin();
             i != fields_.end(); ++i, ++j) {
            if (i->first != j->first)
                return false;
            if (i->second.first != j->second.first)
                return false;
            if (*i->second.second != *j->second.second)
                return false;
        }
        return true;
    }

    template <typename... Rest>
    void add(UnionArm&& field, Rest... rest)
    {
        add(move(field));
        add(move(rest)...);
    }

    void add(UnionArm&& field)
    {
        for (const auto& v: field.first)
            values_.push_back(v);
        fields_.emplace_back(move(field));
    }

    const Declaration& discriminant() const { return discriminant_; }

private:
    Declaration discriminant_;
    vector<UnionArm> fields_;
    vector<shared_ptr<Value>> values_;
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
        body_->printFields(indent, str);
        str << indent << "~" << name_ << "() { _clear(); }" << endl;
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

    void print(Indent indent, ostream& str) const
    {
        str << indent << "//";
        retType_->print(indent, str);
        str << " " << name_ << "(";
        string sep = "";
        for (const auto& argType: argTypes_) {
            str << sep;
            sep = ", ";
            argType->print(indent, str);
        }
        str << ") = " << proc_ << ";" << endl;
    }

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

class ProgramVersion
{
public:
    ProgramVersion(const string& name, int vers = 0)
        : name_(name),
          vers_(vers)
    {
    }

    void print(Indent indent, ostream& str) const
    {
        str << indent << "//version " << name_ << " {" << endl;
        ++indent;
        for (const auto& proc: procs_)
            proc->print(indent, str);
        --indent;
        str << indent << "//} = " << vers_ << ";" << endl;
    }

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
