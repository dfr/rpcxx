/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
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
