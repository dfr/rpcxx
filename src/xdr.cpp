#include <rpc++/xdr.h>

using namespace oncrpc;

XdrMemory::XdrMemory(size_t sz)
{
    storage_ = std::make_unique<uint8_t[]>(sz);
    size_ = sz;
    buf_ = storage_.get();
    next_ = buf_;
    limit_ = buf_ + sz;
}

XdrMemory::XdrMemory(uint8_t* p, size_t sz)
{
    size_ = sz;
    buf_ = p;
    next_ = p;
    limit_ = p + sz;
}

void
XdrMemory::checkSpace(size_t len)
{
    if (next_ + len > limit_)
	throw XdrError("overflow");
}

void
XdrMemory::putWord(uint32_t v)
{
    checkSpace(sizeof(uint32_t));
    *reinterpret_cast<XdrWord*>(next_) = v;
    next_ += sizeof(uint32_t);
}

void
XdrMemory::putBytes(const uint8_t* p, size_t len)
{
    auto len4 = __round(len);
    checkSpace(len4);
    std::copy_n(p, len, next_);
    std::fill_n(next_ + len, len4 - len, 0);
    next_ += len4;
}

void
XdrMemory::getWord(uint32_t& v)
{
    checkSpace(sizeof(uint32_t));
    v = *reinterpret_cast<const XdrWord*>(next_);
    next_ += sizeof(uint32_t);
}

void
XdrMemory::getBytes(uint8_t* p, size_t len)
{
    auto len4 = __round(len);
    checkSpace(len4);
    std::copy_n(next_, len, p);
    next_ += len4;
}

