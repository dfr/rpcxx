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
#include <vector>

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

enum IdentifierType {
    LCAMEL,
    UCAMEL,
    LUNDERSCORE,
    UUNDERSCORE
};

std::vector<std::string> parseNamespaces(const std::string& namespaces);
std::vector<std::string> parseIdentifier(const std::string& identifier);
std::string formatIdentifier(
    IdentifierType type, const std::vector<std::string>& parsed);

}
}
