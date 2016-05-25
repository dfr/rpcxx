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
