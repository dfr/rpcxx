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
