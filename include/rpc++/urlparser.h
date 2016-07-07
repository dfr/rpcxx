/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

// -*- c++ -*-
#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace oncrpc {

struct UrlParser
{
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
    std::map<std::string, std::string> query;

private:
    static std::unordered_set<std::string> hostbasedSchemes;
    static std::unordered_set<std::string> pathbasedSchemes;
};

}
