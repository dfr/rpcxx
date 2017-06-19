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

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <sstream>

#include "utils.h"

using namespace std;

vector<string>
oncrpc::rpcgen::parseNamespaces(const string& namespaces)
{
    string ns = namespaces;
    vector<string> res;
    size_t i;
    while ((i = ns.find("::")) != string::npos) {
        res.push_back(ns.substr(0, i));
        ns = ns.substr(i + 2);
    }
    res.push_back(ns);
    for (const auto& s: res) {
        if (s.size() == 0 ||
            !(std::isalpha(s[0]) || s[0] == '_') ||
            !all_of(s.begin() + 1, s.end(),
                    [](char ch) {
                        return std::isalnum(ch) || ch == '_';
                    })) {
            ostringstream ss;
            ss << "rpcgen: malformed namespace: " << namespaces;
            throw runtime_error(ss.str());
        }
    }
    return res;
}

vector<string>
oncrpc::rpcgen::parseIdentifier(const string& identifier)
{
    vector<string> res;
    string word;
    bool wasLower = false;

    for (auto ch: identifier) {
        if (word.size() > 0) {
            if ((wasLower && std::isupper(ch)) || ch == '_') {
                res.push_back(move(word));
                word.clear();
            }
            if (ch == '_')
                continue;
        }
        wasLower = std::islower(ch);
        word += std::tolower(ch);
    }
    if (word.size() > 0)
        res.push_back(move(word));
    return res;
}

std::string
oncrpc::rpcgen::formatIdentifier(
    IdentifierType type, const std::vector<std::string>& parsed)
{
    bool first = true;
    std::string res;

    switch (type) {
    case LCAMEL:
    case UCAMEL:
        for (const auto& word: parsed) {
            if (type == UCAMEL || !first) {
                res += std::toupper(word[0]);
                res += word.substr(1);
            }
            else {
                res = word;
            }
            first = false;
        }
        break;

    case LUNDERSCORE:
        for (const auto& word: parsed) {
            if (!first)
                res += "_";
            res += word;
            first = false;
        }
        break;

    case UUNDERSCORE:
        for (const auto& word: parsed) {
            if (!first)
                res += "_";
            for (auto ch: word)
                res += std::toupper(ch);
            first = false;
        }
        break;
    }
    return res;
}
