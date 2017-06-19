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

#include <cstddef>
#include <functional>
#include <rpc++/xdr.h>

namespace oncrpc {

class RecordWriter: public XdrSink
{
public:
    RecordWriter(
        size_t buflen,
        std::function<ptrdiff_t(const void*, size_t)> flush);

    void pushRecord();

    // XdrSink overrides
    void flush() override;

private:
    void checkSpace(size_t len);

    void startNewFragment();

    void flush(bool endOfRecord);

private:
    std::vector<uint8_t> buf_;
    std::function<ptrdiff_t(const void*, size_t)> flush_;
};

class RecordReader: public XdrSource
{
public:
    RecordReader(size_t buflen, std::function<ptrdiff_t(void*, size_t)> fill);

    /// Discard the rest of the current record, if any and set up to
    /// start reading a new record
    void skipRecord();

    /// Like skipRecord but ensures we have already read the whole of
    /// this record
    void endRecord();

    // XdrSource overrides
    size_t readSize() const override
    {
        // Right now, we don't need this to be accurate - its only really
        // needed for accounting in the server and RecordReader is not used
        // in that context
        return 0;
    }
    void fill() override;

private:
    void need(size_t sz);

private:
    std::vector<uint8_t> buf_;
    uint8_t* bufferLimit_;
    std::function<ptrdiff_t(void*, size_t)> fill_;
    size_t fragBuffered_;
    size_t fragRemaining_;
    bool lastFragment_;
};

}
