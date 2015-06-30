#include <rpc++/xdr.h>

using namespace oncrpc;

XdrMemory::XdrMemory(size_t sz)
{
    storage_ = std::make_unique<uint8_t[]>(sz);
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
