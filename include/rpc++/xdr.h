// -*- c++ -*-

#pragma once

#include <cassert>
#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

namespace oncrpc {

using std::uint8_t;
using std::uint32_t;
using std::uint64_t;

#include <machine/endian.h>

/// These templates are used to implement bounded strings and arrays
/// (e.g. string<512> or int<10> in the XDR language).
template <size_t N>
class bounded_string: public std::string
{
};

template <typename T, size_t N>
class bounded_vector: public std::vector<T>
{
};

class XdrError: public std::runtime_error
{
public:
    XdrError(const std::string& what)
        : std::runtime_error(what)
    {
    }

    XdrError(const char* what)
        : std::runtime_error(what)
    {
    }
};

/// A 32-bit word stored in network byte order
class XdrWord
{
public:
    explicit XdrWord(uint32_t v)
    {
        *this = v;
    }

    XdrWord& operator=(uint32_t v)
    {
#if BYTE_ORDER == LITTLE_ENDIAN
        v = __builtin_bswap32(v);
#endif
        value_ = v;
        return *this;
    }

    operator uint32_t() const
    {
#if BYTE_ORDER == LITTLE_ENDIAN
        return __builtin_bswap32(value_);
#else
        return value_;
#endif
    }

    const void* data() const { return &value_; }
    void* data() { return &value_; }

private:
    uint32_t value_;
};

template <typename T>
inline T __round(T len)
{
    return (len + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1);
}

class XdrSink
{
public:
    ~XdrSink() {}

    /// Write a 32-bit word to the stream
    void putWord(const uint32_t v)
    {
        if (writeCursor_ + sizeof(v) > writeLimit_)
            flush();
        *reinterpret_cast<XdrWord*>(writeCursor_) = v;
        writeCursor_ += sizeof(v);
    }

    /// Write a sequence of bytes to the stream and pad to the nearest
    /// 32-bit boundary with zeros
    void putBytes(const uint8_t* p, size_t len)
    {
        size_t pad = __round(len) - len;
        while (len > 0) {
            if (writeCursor_ == writeLimit_)
                flush();
            size_t n = writeLimit_ - writeCursor_;
            if (n > len)
                n = len;
            std::copy_n(p, n, writeCursor_);
            p += n;
            writeCursor_ += n;
            len -= n;
        }
        while (pad > 0) {
            if (writeCursor_ == writeLimit_)
                flush();
            size_t n = writeLimit_ - writeCursor_;
            if (n > pad)
                n = pad;
            std::fill_n(writeCursor_, n, 0);
            writeCursor_ += n;
            pad -= n;
        }
    }

    void putBytes(const void* p, size_t len)
    {
        putBytes(static_cast<const uint8_t*>(p), len);
    }

    /// If there is at least len bytes of space in the write buffer,
    /// return a pointer to the next free byte and advance the write
    /// cursor by len bytes. If not, return nullptr. Len must be a
    /// multiple of sizeof(XdrWord).
    template <typename T>
    T* writeInline(size_t len)
    {
        assert(len == __round(len));
        T* p = nullptr;
        if (writeCursor_ + len <= writeLimit_) {
            p = reinterpret_cast<T*>(writeCursor_);
            writeCursor_ += len;
        }
        return p;
    }

    virtual void flush() = 0;

protected:
    uint8_t* writeCursor_;
    uint8_t* writeLimit_;
};

class XdrSource
{
public:
    ~XdrSource() {}

    /// Read a 32-bit word from the stream
    void getWord(uint32_t& v)
    {
        if (readCursor_ + sizeof(v) > readLimit_) {
            uint8_t buf[sizeof(v)];
            getBytes(buf, sizeof(v));
            v = *reinterpret_cast<const XdrWord*>(buf);
        }
        else {
            v = *reinterpret_cast<const XdrWord*>(readCursor_);
            readCursor_ += sizeof(v);
        }
    }

    /// Read a sequence of bytes from the stream and skip the
    /// following padding up to the next 32-bit boundary
    void getBytes(uint8_t* p, size_t len)
    {
        size_t pad = __round(len) - len;
        while (len > 0) {
            if (readCursor_ == readLimit_)
                fill();
            size_t n = readLimit_ - readCursor_;
            if (n > len)
                n = len;
            std::copy_n(readCursor_, n, p);
            p += n;
            readCursor_ += n;
            len -= n;
        }
        while (pad > 0) {
            if (readCursor_ == readLimit_)
                fill();
            size_t n = readLimit_ - readCursor_;
            if (n > pad)
                n = pad;
            readCursor_ += n;
            pad -= n;
        }
    }

    void getBytes(void* p, size_t len)
    {
        getBytes(static_cast<uint8_t*>(p), len);
    }

    virtual void fill() = 0;

protected:
    const uint8_t* readCursor_;
    const uint8_t* readLimit_;
};

/// Expands to either T& or const T& depending on whether XDR is
/// XdrSource or XdrSink
template <typename T, typename XDR>
using RefType = typename std::conditional<
    std::is_convertible<XDR*, XdrSink*>::value, const T&, T&>::type;

inline void xdr(const uint32_t v, XdrSink* xdrs)
{
    xdrs->putWord(v);
}

inline void xdr(uint32_t& v, XdrSource* xdrs)
{
    xdrs->getWord(v);
}

inline void
xdr(const uint64_t v, XdrSink* xdrs)
{
    xdrs->putWord(static_cast<uint32_t>(v >> 32));
    xdrs->putWord(static_cast<uint32_t>(v));
}

inline void xdr(uint64_t& v, XdrSource* xdrs)
{
    uint32_t v0, v1;
    xdrs->getWord(v0);
    xdrs->getWord(v1);
    v = (static_cast<uint64_t>(v0) << 32) | v1;
}

template <size_t N>
inline void xdr(const std::array<uint8_t, N>& v, XdrSink* xdrs)
{
    xdrs->putBytes(v.data(), v.size());
}

template <size_t N>
inline void xdr(std::array<uint8_t, N>& v, XdrSource* xdrs)
{
    xdrs->getBytes(v.data(), v.size());
}

inline void xdr(const std::vector<uint8_t>& v, XdrSink* xdrs)
{
    xdrs->putWord(v.size());
    xdrs->putBytes(v.data(), v.size());
}

inline void xdr(std::vector<uint8_t>& v, XdrSource* xdrs)
{
    uint32_t len;
    xdrs->getWord(len);
    v.resize(len);
    xdrs->getBytes(v.data(), v.size());
}

template <size_t N>
inline void xdr(const bounded_vector<uint8_t, N>& v, XdrSink* xdrs)
{
    assert(v.size() <= N);
    xdrs->putWord(v.size());
    xdrs->putBytes(v.data(), v.size());
}

template <size_t N>
inline void xdr(bounded_vector<uint8_t, N>& v, XdrSource* xdrs)
{
    uint32_t len;
    xdrs->getWord(len);
    if (len > N)
        throw XdrError("array overflow");
    v.resize(len);
    xdrs->getBytes(v.data(), v.size());
}

inline void xdr(const std::string& v, XdrSink* xdrs)
{
    xdrs->putWord(v.size());
    xdrs->putBytes(reinterpret_cast<const uint8_t*>(v.data()), v.size());
}

inline void xdr(std::string& v, XdrSource* xdrs)
{
    uint32_t len;
    xdrs->getWord(len);
    v.resize(len);
    xdrs->getBytes(reinterpret_cast<uint8_t*>(&v[0]), v.size());
}

template <size_t N>
inline void xdr(const bounded_string<N>& v, XdrSink* xdrs)
{
    assert(v.size() <= N);
    xdrs->putWord(v.size());
    xdrs->putBytes(reinterpret_cast<const uint8_t*>(v.data()), v.size());
}

template <size_t N>
inline void xdr(bounded_string<N>& v, XdrSource* xdrs)
{
    uint32_t len;
    xdrs->getWord(len);
    if (len > N)
        throw XdrError("string overflow");
    v.resize(len);
    xdrs->getBytes(reinterpret_cast<uint8_t*>(&v[0]), v.size());
}

inline void xdr(const int v, XdrSink* xdrs)
{
    xdr(reinterpret_cast<const uint32_t&>(v), xdrs);
}

inline void xdr(int& v, XdrSource* xdrs)
{
    xdr(reinterpret_cast<uint32_t&>(v), xdrs);
}

inline void xdr(const long v, XdrSink* xdrs)
{
    xdr(reinterpret_cast<const uint64_t&>(v), xdrs);
}

inline void xdr(long& v, XdrSource* xdrs)
{
    xdr(reinterpret_cast<uint64_t&>(v), xdrs);
}

inline void xdr(const unsigned long v, XdrSink* xdrs)
{
    xdr(reinterpret_cast<const uint64_t&>(v), xdrs);
}

inline void xdr(unsigned long& v, XdrSource* xdrs)
{
    xdr(reinterpret_cast<uint64_t&>(v), xdrs);
}

inline void xdr(const float v, XdrSink* xdrs)
{
    xdr(reinterpret_cast<const uint32_t&>(v), xdrs);
}

inline void xdr(float& v, XdrSource* xdrs)
{
    xdr(reinterpret_cast<uint32_t&>(v), xdrs);
}

inline void xdr(const double v, XdrSink* xdrs)
{
    xdr(reinterpret_cast<const uint64_t&>(v), xdrs);
}

inline void xdr(double& v, XdrSource* xdrs)
{
    xdr(reinterpret_cast<uint64_t&>(v), xdrs);
}

inline void xdr(const bool v, XdrSink* xdrs)
{
    const uint32_t t = v;
    xdr(t, xdrs);
}

inline void xdr(bool& v, XdrSource* xdrs)
{
    uint32_t t;
    xdr(t, xdrs);
    v = t;
}

template <typename T, size_t N>
inline void xdr(const std::array<T, N>& v, XdrSink* xdrs)
{
    for (const auto& e : v)
        xdr(e, xdrs);
}

template <typename T, size_t N>
inline void xdr(std::array<T, N>& v, XdrSource* xdrs)
{
    for (auto& e : v)
        xdr(e, xdrs);
}

template <typename T>
inline void xdr(const std::vector<T>& v, XdrSink* xdrs)
{
    uint32_t sz = v.size();
    xdr(sz, xdrs);
    for (const auto& e : v)
        xdr(e, xdrs);
}

template <typename T>
inline void xdr(std::vector<T>& v, XdrSource* xdrs)
{
    uint32_t sz;
    xdr(sz, xdrs);
    v.resize(sz);
    for (auto& e : v)
        xdr(e, xdrs);
}

template <typename T, size_t N>
inline void xdr(const bounded_vector<T, N>& v, XdrSink* xdrs)
{
    uint32_t sz = v.size();
    assert(sz <= N);
    xdr(sz, xdrs);
    for (const auto& e : v)
        xdr(e, xdrs);
}

template <typename T, size_t N>
inline void xdr(bounded_vector<T, N>& v, XdrSource* xdrs)
{
    uint32_t sz;
    xdr(sz, xdrs);
    if (sz > N)
        throw XdrError("array overflow");
    v.resize(sz);
    for (auto& e : v)
        xdr(e, xdrs);
}

template <typename T>
inline void xdr(const std::unique_ptr<T>& v, XdrSink* xdrs)
{
    if (v) {
        xdr(true, xdrs);
        xdr(*v, xdrs);
    }
    else {
        xdr(false, xdrs);
    }
}

template <typename T>
inline void xdr(std::unique_ptr<T>& v, XdrSource* xdrs)
{
    bool notNull;
    xdr(notNull, xdrs);
    if (notNull) {
        v.reset(new T);
        xdr(*v, xdrs);
    }
    else {
        v.reset(nullptr);
    }
}

class XdrMemory: public XdrSink, public XdrSource
{
public:
    /// Create a memory encoder/decoder which owns its storage
    XdrMemory(size_t sz);

    /// Create a memory encoder/decoder which uses external storage
    XdrMemory(uint8_t* p, size_t sz);
    XdrMemory(void* p, size_t sz)
        : XdrMemory(static_cast<uint8_t*>(p), sz)
    {
    }

    uint8_t* buf() const
    {
        return buf_;
    }

    void rewind()
    {
        writeCursor_ = buf_;
        readCursor_ = buf_;
    }

    void setWriteSize(size_t sz)
    {
        assert(sz <= size_);
        writeLimit_ = buf_ + sz;
    }

    void setReadSize(size_t sz)
    {
        assert(sz <= size_);
        readLimit_ = buf_ + sz;
    }

    size_t bufferSize() const
    {
        return size_;
    }
    
    size_t writePos() const
    {
        return writeCursor_ - buf_;
    }

    size_t readPos() const
    {
        return readCursor_ - buf_;
    }

    // XdrSink overrides
    void flush() override;

    // XdrSource overrides
    void fill() override;

private:
    std::unique_ptr<uint8_t[]> storage_;
    size_t size_;
    uint8_t* buf_;
};

class XdrSizer: public XdrMemory
{
public:
    XdrSizer()
        : XdrMemory(1024)
    {
    }

    size_t size() const
    {
        return size_ + writePos();
    }

    // XdrSink overrides
    void flush() override
    {
        size_ += writePos();
        rewind();
    }

private:
    size_t size_ = 0;
};

template <typename T>
size_t XdrSizeof(const T& v)
{
    XdrSizer xdrs;
    xdr(v, &xdrs);
    return xdrs.size();
}

}

