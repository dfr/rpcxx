// -*- c++ -*-

#pragma once

#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rpc++/rec.h>

namespace std {

template <>
class hash<std::pair<uint32_t, uint32_t>>
{
public:
    size_t operator()(const std::pair<uint32_t, uint32_t>& v) const
    {
        std::hash<uint32_t> h;
        return h(v.first) ^ h(v.second);
    }
};

}

namespace oncrpc {

typedef std::function<bool(uint32_t, XdrSource*, XdrSink*)> Service;

struct ServiceEntry
{
    Service handler;
    std::unordered_set<uint32_t> procs;
};

class ServiceRegistry
{
public:
    void add(
        uint32_t prog, uint32_t vers, ServiceEntry&& entry);

    void remove(uint32_t prog, uint32_t vers);

    const ServiceEntry* lookup(uint32_t prog, uint32_t vers) const;

    // Process an rpc message, returning true if there is a reply
    bool process(XdrSource* xdrin, XdrSink* xdrout);
    bool process(rpc_msg&& msg, XdrSource* xdrin, XdrSink* xdrout);

private:
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> programs_;
    std::unordered_map<std::pair<uint32_t, uint32_t>, ServiceEntry> services_;
};

}
