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
