// -*- c++ -*-

#pragma once

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
