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

#pragma once

#include <string>

#include "utils.h"
#include "values.h"

namespace oncrpc {
namespace rpcgen {

using namespace ::std;

class Type
{
public:
    virtual ~Type() {}
    virtual bool isPOD() const = 0;
    virtual bool isVoid() const { return false; }
    virtual bool isOneway() const { return false; }
    virtual void print(Indent indent, ostream& str) const = 0;
    virtual void forwardDeclarations(Indent indent, ostream& str) const {}

    /// Strip type aliases
    virtual const Type* underlyingType() const
    {
	return this;
    }

    string name() const
    {
        ostringstream ss;
        print(Indent(), ss);
        return ss.str();
    }

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

class TypeAlias: public Type
{
public:
    TypeAlias(const string& name, shared_ptr<Type> ty)
        : name_(name), ty_(ty)
    {
    }

    bool isPOD() const override
    {
	return ty_->isPOD();
    }

    void print(Indent indent, ostream& str) const override
    {
        str << name_;
    }

    const Type* underlyingType() const override
    {
	return ty_.get();
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const TypeAlias*>(&other);
	if (p)
	    return name_ == p->name_;
	return *ty_->underlyingType() == *other.underlyingType();
    }

protected:
    string name_;
    shared_ptr<Type> ty_;
};

class NamedType: public Type
{
public:
    NamedType(const string& name)
        : name_(name)
    {
    }

    bool isPOD() const override
    {
        // ideally, we look up the type
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << name_;
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const NamedType*>(other.underlyingType());
        if (p)
            return name_ == p->name_;
        return false;
    }

protected:
    string name_;
};

class NamedStructType: public NamedType
{
public:
    NamedStructType(const string& name)
        : NamedType(name)
    {
    }

    bool isPOD() const override
    {
        // ideally, we look up the type
        return false;
    }

    void forwardDeclarations(Indent indent, ostream& str) const override
    {
        str << indent << "struct " << name_ << ";" << endl;
    }
};

class NamedUnionType: public NamedType
{
public:
    NamedUnionType(const string& name)
        : NamedType(name)
    {
    }

    bool isPOD() const override
    {
        // ideally, we look up the type
        return false;
    }

    void forwardDeclarations(Indent indent, ostream& str) const override
    {
        str << indent << "union " << name_ << ";" << endl;
    }
};

class VoidType: public Type
{
public:
    VoidType()
    {
    }

    bool isPOD() const override
    {
        return true;
    }

    bool isVoid() const override
    {
        return true;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << "void";
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const VoidType*>(&other);
        if (p)
            return true;
        return false;
    }
};

class OnewayType: public Type
{
public:
    OnewayType()
    {
    }

    bool isPOD() const override
    {
        return true;
    }

    bool isVoid() const override
    {
        return true;
    }

    bool isOneway() const override
    {
        return true;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << "void";
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const OnewayType*>(&other);
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

    bool isPOD() const override
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << "std::unique_ptr<" << *type_ << ">";
    }

    void forwardDeclarations(Indent indent, ostream& str) const override
    {
        type_->forwardDeclarations(indent, str);
    }

    int operator==(const Type& other) const override
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

    bool isPOD() const override
    {
        return true;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << "std::";
        if (!isSigned_)
            str << "u";
        str << "int" << width_ << "_t";
    }

    int operator==(const Type& other) const override
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

    bool isPOD() const override
    {
        return true;
    }

    void print(Indent indent, ostream& str) const override
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

    int operator==(const Type& other) const override
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

    bool isPOD() const override
    {
        return true;
    }

    void print(Indent indent, ostream& str) const override
    {
        str << "int /* bool */";
    }

    int operator==(const Type& other) const override
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

    bool isPOD() const override
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
        if (isFixed_)
            str << "std::array<std::uint8_t, " << *size_ << ">";
        else if (size_)
            str << "oncrpc::bounded_vector<std::uint8_t, " << *size_ << ">";
        else
            str << "std::vector<std::uint8_t>";
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const OpaqueType*>(&other);
        if (p)
            return (((size_ == nullptr && p->size_ == nullptr)
		     || *size_.get() == *p->size_.get())
		    && isFixed_ == p->isFixed_);
        return false;
    }

private:
    shared_ptr<Value> size_;
    bool isFixed_;
};

class OpaqueRefType: public Type
{
public:
    OpaqueRefType()
    {
    }

    OpaqueRefType(shared_ptr<Value>&& size)
        : size_(move(size))
    {
    }

    bool isPOD() const override
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
        // XXX handle size_
        str << "std::shared_ptr<oncrpc::Buffer>";
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const OpaqueRefType*>(&other);
        if (p)
            return *size_.get() == *p->size_.get();
        return false;
    }

private:
    shared_ptr<Value> size_;
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

    bool isPOD() const override
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
        if (size_)
            str << "oncrpc::bounded_string<" << *size_ << ">";
        else
            str << "std::string";
    }

    int operator==(const Type& other) const override
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

    bool isPOD() const override
    {
        return type_->isPOD();
    }

    void print(Indent indent, ostream& str) const override
    {
        if (isFixed_)
            str << "std::array<" << *type_ << ", " << *size_ << ">";
        else if (size_)
            str << "oncrpc::bounded_vector<" << *type_ << ", " << *size_ << ">";
        else
            str << "std::vector<" << *type_ << ">";
    }

    int operator==(const Type& other) const override
    {
        auto p = dynamic_cast<const ArrayType*>(&other);
        if (p) {
            if (*type_ != *p->type_)
                return false;
            if (size_) {
                if (*size_ != *p->size_)
                    return false;
            }
            else {
                if (p->size_)
                    return false;
            }
            return *type_ == *p->type_ &&
                isFixed_ == p->isFixed_;
        }
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

    bool isPOD() const override
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

    bool isPOD() const override
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

    ValueList& operator=(ValueList&& other)
    {
        values_ = move(other.values_);
        return *this;
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

struct UnionArm
{
    UnionArm(ValueList&& values, Declaration&& decl)
        : values_(move(values)), decl_(move(decl))
    {
    }

    ValueList values_;
    Declaration decl_;
};

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

    bool isPOD() const override
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
#ifndef NDEBUG
        for (const auto& field: fields_) {
            // Throw an exception with line number etc
            assert(field.decl_.second->isPOD());
        }
#endif
        str << indent << "struct {" << endl;
        printFields(indent + 1, "", str);
        str << indent << "}" << endl;
    }

    void discriminantOk(const ValueList& values, ostream& str) const
    {
        str << "(";
        bool first = true;
        if (values.size() > 0) {
            for (const auto& val: values) {
                if (!first)
                    str << " || ";
                str << discriminant_.first << " == " << *val;
                first = false;
            }
        }
        else {
            // If there are no values we must check that the discriminant is
            // not any of the other field values
            for (const auto& val: values_) {
                if (!first)
                    str << " && ";
                str << discriminant_.first << " != " << *val;
                first = false;
            }
        }
        str << ")";
    }

    void checkDiscriminant(
        Indent indent, const ValueList& values, ostream& str) const
    {
        str << indent << "assert";
	discriminantOk(values, str);
        str << ";" << endl;
    }

    void printFields(Indent indent, const string& name, ostream& str) const
    {
        // Default constructor
        str << indent << name << "() {}" << endl;

        // Move constructor
        str << indent << name << "(" << name << "&& other) {" << endl;
        ++indent;
        str << indent << discriminant_.first
            << " = other." << discriminant_.first << ";" << endl;
        str << indent << "if (!other._hasValue) return;" << endl;
        printSwitch(
            indent, str, "",
            [&str](Indent indent, auto name, auto type)
            {
                if (name.size() == 0)
                    return;
                str << indent << "new(&_storage) "
                    << *type << "(std::move(other." << name << "()));" << endl;
            });
        str << indent << "_hasValue = true;" << endl;
        str << indent << "other._clear();" << endl;
        --indent;
        str << indent << "}" << endl;

        // Type-specific constructors. We need to make sure we only
        // emit one constructor if there is more than one field with
        // the same type.
	vector<bool> handled(fields_.size(), false);
	for (size_t i = 0; i < fields_.size(); i++) {
	    if (handled[i])
		continue;
	    auto& field = fields_[i];
	    handled[i] = true;
	    if (field.decl_.first.size() == 0) {
		str << indent << name << "(" << *discriminant_.second
		    << " _discriminant)" << endl;
		++indent;
		str << indent << ": " << discriminant_.first
		    << "(_discriminant) {" << endl;
		str << indent << "assert(";
		discriminantOk(field.values_,str);
		for (size_t j = i + 1; j < fields_.size(); j++) {
		    if (handled[j])
			continue;
		    auto& field2 = fields_[j];
		    if (field2.decl_.first.size() == 0) {
			str << endl << indent << "|| ";
			discriminantOk(field2.values_, str);
			handled[j] = true;
		    }
		}
		str << ");" << endl;
		str << indent << "_hasValue = true;" << endl;
		--indent;
		str << indent << "}" << endl;
	    }
            else {
		ValueList values;
		for (auto v: field.values_)
		    values.add(move(v));
		for (size_t j = i + 1; j < fields_.size(); j++) {
		    if (handled[j])
			continue;
		    auto& field2 = fields_[j];
		    if (*field.decl_.second == *field2.decl_.second) {
			for (auto v: field2.values_)
			    values.add(move(v));
			handled[j] = true;
		    }
		}
                str << indent << name << "(" << *discriminant_.second
                    << " _discriminant, "
                    << *field.decl_.second << "&& _value)" << endl;
                ++indent;
                str << indent << ": " << discriminant_.first
                    << "(_discriminant) {" << endl;
                checkDiscriminant(indent, values, str);
                str << indent << "new (&_storage) "
                    << *field.decl_.second << "(std::move(_value));" << endl;
                str << indent << "_hasValue = true;" << endl;
                --indent;
                str << indent << "}" << endl;

            }
        }

        // Destructor
        str << indent << "~" << name << "() { _clear(); }" << endl;

        // Move assignment
        str << indent << name << "& operator=("
            << name << "&& other) {" << endl;
        ++indent;
        str << indent << "_clear();" << endl;
        str << indent << discriminant_.first
            << " = other." << discriminant_.first << ";" << endl;
        printSwitch(
            indent, str, "",
            [&str](Indent indent, auto name, auto type)
            {
                if (name.size() == 0)
                    return;
                str << indent << "new(&_storage) "
                    << *type << "(std::move(other." << name << "()));" << endl;
            });
        str << indent << "_hasValue = true;" << endl;
        str << indent << "other._clear();" << endl;
        str << indent << "return *this;" << endl;
        --indent;
        str << indent << "}" << endl;
        printFields(indent, str);
    }

    void printFields(Indent indent, ostream& str) const
    {
        str << indent;
        discriminant_.second->print(indent, str);
        str << " " << discriminant_.first << ";" << endl;
        str << indent << "void set_" << discriminant_.first << "(";
        discriminant_.second->print(indent, str);
        str << " _v) { _setType(_v); }" << endl;
        str << indent << "union _u {" << endl;
        ++indent;
        for (const auto& field: fields_) {
            if (field.values_.size() > 0) {
                for (const auto& val: field.values_) {
                    str << indent
                        << "// case " << *val << ":" << endl;
                }
            }
            else {
                str << indent
                    << "// default:" << endl;
            }
            // If we have no name, its a void field so ignore it
            if (field.decl_.first.size() > 0) {
                str << indent;
                field.decl_.second->print(indent + 4, str);
                str << " " << field.decl_.first << ";" << endl;
            }
        }
        --indent;
        str << indent << "};" << endl;
        str << indent << "std::aligned_union<" << endl;
        ++indent;
        str << indent << "sizeof(_u)";
        for (const auto& field: fields_) {
            if (field.decl_.first.size() == 0)
                continue;
            str << "," << endl << indent;
            field.decl_.second->print(indent + 4, str);
        }
        --indent;
        str << ">::type _storage;" << endl;
        str << indent << "bool _hasValue = false;" << endl;

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
                if (type->isPOD())
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
        str << indent << discriminant_.first << " = _v;" << endl;
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
        --indent;
        str << indent << "}" << endl;
    }

    void printAccessors(Indent indent, ostream& str, bool isConst) const
    {
        string attr = "";
        if (isConst)
            attr = "const ";
        for (const auto& field: fields_) {
            if (field.decl_.first.size() == 0)
                continue;
            str << indent << attr;
            field.decl_.second->print(indent, str);
            str << "& " << field.decl_.first << "() " << attr << "{" << endl;
            ++indent;
            str << indent << "assert(_hasValue);" << endl;
            checkDiscriminant(indent, field.values_, str);
            str << indent << "return *reinterpret_cast<" << attr;
            field.decl_.second->print(indent, str);
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
        bool hasDefault = false;
        for (const auto& field: fields_) {
            if (field.values_.size() > 0) {
                for (const auto& val: field.values_) {
                    str << indent << "case " << *val << ":" << endl;
                }
            }
            else {
                hasDefault = true;
                str << indent << "default:" << endl;
            }
            ++indent;
            fn(indent, field.decl_.first, field.decl_.second);
            str << indent << "break;" << endl;
            --indent;
        }
        if (!hasDefault) {
            str << indent << "default:" << endl;
            ++indent;
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
            if (i->values_ != j->values_)
                return false;
            if (i->decl_.first != j->decl_.first)
                return false;
            if (*i->decl_.second != *j->decl_.second)
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
        for (const auto& v: field.values_)
            values_.push_back(v);
        fields_.emplace_back(move(field));
    }

    const Declaration& discriminant() const { return discriminant_; }

private:
    Declaration discriminant_;
    vector<UnionArm> fields_;
    vector<shared_ptr<Value>> values_;
};

}
}
