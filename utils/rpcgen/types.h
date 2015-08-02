#pragma once

#include <string>

#include "utils/rpcgen/utils.h"
#include "utils/rpcgen/values.h"

namespace oncrpc {
namespace rpcgen {

using namespace ::std;

class Type
{
public:
    virtual ~Type() {}
    virtual bool isPOD() const = 0;
    virtual bool isVoid() const { return false; }
    virtual void print(Indent indent, ostream& str) const = 0;

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
        else
            str << "oncrpc::bounded_vector<" << *type_ << ", " << *size_ << ">";
    }

    int operator==(const Type& other) const override
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

    bool isPOD() const override
    {
        return false;
    }

    void print(Indent indent, ostream& str) const override
    {
#ifndef NDEBUG
        for (const auto& field: fields_) {
            // Throw an exception with line number etc
            assert(field.second.second->isPOD());
        }
#endif
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

}
}
