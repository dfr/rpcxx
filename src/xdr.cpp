/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <rpc++/xdr.h>

using namespace oncrpc;

XdrSink::~XdrSink()
{
}

XdrSource::~XdrSource()
{
}

XdrMemory::XdrMemory(size_t sz)
    : storage_((uint8_t*) std::malloc(sz),
               [](uint8_t* p) { std::free(p); })
{
    size_ = sz;
    buf_ = storage_.get();
    writeCursor_ = buf_;
    writeLimit_ = buf_ + sz;
    readCursor_ = buf_;
    readLimit_ = buf_ + sz;
}

XdrMemory::XdrMemory(uint8_t* p, size_t sz)
{
    size_ = sz;
    buf_ = p;
    writeCursor_ = p;
    writeLimit_ = p + sz;
    readCursor_ = buf_;
    readLimit_ = buf_ + sz;
}

XdrMemory::XdrMemory(const uint8_t* p, size_t sz)
{
    size_ = sz;
    buf_ = const_cast<uint8_t*>(p);
    writeCursor_ = nullptr;
    writeLimit_ = nullptr;
    readCursor_ = buf_;
    readLimit_ = buf_ + sz;
}

void
XdrMemory::flush()
{
    throw XdrError("overflow");
}

void
XdrMemory::fill()
{
    throw XdrError("overflow");
}
