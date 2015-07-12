#pragma once

#include <string>

#include "utils/rpcgen/utils.h"

namespace oncrpc {
namespace rpcgen {

using namespace ::std;

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

}
}
