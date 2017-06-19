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

// -*- c++ -*-
#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace oncrpc {

class UrlParser
{
public:
    UrlParser();
    UrlParser(const std::string& url);
    void parse(const std::string& url);
    bool isHostbased();
    bool isPathbased();
    bool hasScheme(const std::string& s);
    void parseScheme(std::string& s);
    void parseHost(std::string& s);
    void parseIPv4(std::string& s);
    void parseIPv6(std::string& s);
    void parsePort(std::string& s);
    void parsePath(std::string& s);
    void parseQuery(std::string& s);
    void parseQueryTerm(const std::string& s);

    static void addHostbasedScheme(const std::string& scheme);
    static void addPathbasedScheme(const std::string& scheme);

    std::string all;
    std::string scheme;
    std::string schemeSpecific;
    std::string host;
    std::string port;
    std::string path;
    std::vector<std::string> segments;
    std::multimap<std::string, std::string> query;

private:
    static std::unordered_set<std::string> hostbasedSchemes;
    static std::unordered_set<std::string> pathbasedSchemes;
};

}
