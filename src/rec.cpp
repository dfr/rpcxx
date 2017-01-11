/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <cassert>

#include <rpc++/rec.h>

using namespace oncrpc;

template <typename T>
static inline bool isWordAligned(T* p)
{
    return (reinterpret_cast<uintptr_t>(p) & 3) == 0;
}

RecordWriter::RecordWriter(
    size_t buflen, std::function<ptrdiff_t(const void*, size_t)> flush)
{
    assert((buflen & 3) == 0);
    assert(buflen > 4);
    buf_.resize(buflen);
    flush_ = flush;
    startNewFragment();
}

void
RecordWriter::flush()
{
    flush(false);
}

void
RecordWriter::startNewFragment()
{
    writeCursor_ = buf_.data();
    writeLimit_ = writeCursor_ + buf_.size();
    const uint32_t rec = 0;
    xdr(rec, this);
}

void
RecordWriter::flush(bool endOfRecord)
{
    auto len = writeCursor_ - buf_.data();
    assert(len > 4);
    uint32_t rec = len - 4;
    if (endOfRecord)
        rec |= 1 << 31;
    *reinterpret_cast<XdrWord*>(buf_.data()) = rec;
    auto n = flush_(buf_.data(), len);
    if (n != len)
        throw XdrError("short write");
    startNewFragment();
}

void
RecordWriter::pushRecord()
{
    flush(true);
}

RecordReader::RecordReader(
    size_t buflen, std::function<ptrdiff_t(void*, size_t)> fill)
{
    assert((buflen & 3) == 0);
    assert(buflen > 4);
    buf_.resize(buflen);
    fill_ = fill;
    readCursor_ = readLimit_ = bufferLimit_ = buf_.data();
    fragBuffered_ = 0;
    fragRemaining_ = 0;
    lastFragment_ = false;
}

void
RecordReader::skipRecord()
{
#if 0
    debug (rec) {
        writefln("skipRecord(): lastFragment_=%s, fragRemaining_=%d",
                 lastFragment_, fragRemaining_);
    }
#endif
    
    fragRemaining_ -= fragBuffered_;
    fragBuffered_ = 0;

    if (lastFragment_ == true && fragRemaining_ == 0)
        return;
    while (lastFragment_ == false || fragRemaining_ > 0) {
        auto n = readLimit_ - readCursor_;
        readCursor_ = readLimit_;
        fragRemaining_ -= n;
        fragBuffered_ -= n;
        if (lastFragment_ == false || fragRemaining_ > 0)
            fill();
    }
    lastFragment_ = false;
}

void
RecordReader::endRecord()
{
    fragRemaining_ -= fragBuffered_;
    fragBuffered_ = 0;
    assert(lastFragment_ && fragRemaining_ == 0);
    lastFragment_ = false;
}

void
RecordReader::fill()
{
    fragRemaining_ -= fragBuffered_;
    fragBuffered_ = 0;

    // If the fragment is larger than our buffer, we keep track of
    // how much of the fragment is remaining. Note that the
    // fragment length doesn't include the header so we need to
    // allow for that when initialising fragRemaining_.
    //
    // We also need to allow for the opposite situation where our
    // buffer contains multiple fragments which could happen if we
    // have a client buffer up several messages before sending.
    if (fragRemaining_ == 0) {
        // If we have data buffered, try to use it now, otherwise read
        // from upstream. We need to make sure that we have a complete
        // fragment header in our buffer.
        while (readCursor_ + 4 > bufferLimit_) {
            // We need to read more data from upstream. Move what we
            // have to the beginning of our buffer first.
            auto n = bufferLimit_ - readCursor_;
            if (n > 0 && readCursor_ != buf_.data())
                std::copy_n(readCursor_, n, buf_.data());
            readCursor_ = buf_.data();
            bufferLimit_ = const_cast<uint8_t*>(readCursor_ + n);

            n = fill_(bufferLimit_, buf_.size() - n);
            if (n <= 0)
                throw XdrError("end of file");
            bufferLimit_ += n;
        }
    }
    else {
        // Read from upstream
        auto n = fill_(buf_.data(), buf_.size());
        if (n <= 0)
            throw XdrError("end of file");
        readCursor_ = buf_.data();
        bufferLimit_ = const_cast<uint8_t*>(readCursor_ + n);
    }
    if (fragRemaining_ == 0) {
        // We refuse to read past the end of the last fragment until
        // our owner tells us we are finished with the whole
        // record. Note that we have already ensured that we at least
        // have a complete fragment header in our buffer.
        if (lastFragment_)
            throw XdrError("end of record");
        uint32_t rec = *reinterpret_cast<const XdrWord*>(readCursor_);
        readCursor_ += sizeof(rec);
        fragRemaining_ = rec & 0x7fffffff;
        lastFragment_ = (rec & (1 << 31)) != 0;
#if 0
        debug (rec) {
            writefln("New fragment, fragRemaining_=%d, lastFragment_=%s",
                     fragRemaining_, lastFragment_);
        }
#endif
    }

    // Keep track how far we can read from our buffer while
    // remaining in the current fragment
    if (readCursor_ + fragRemaining_ < bufferLimit_) {
        readLimit_ = readCursor_ + fragRemaining_;
        fragBuffered_ = fragRemaining_;
    }
    else {
        readLimit_ = bufferLimit_;
        fragBuffered_ = bufferLimit_ - readCursor_;
    }
}
