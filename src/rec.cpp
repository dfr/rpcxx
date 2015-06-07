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
RecordWriter::putWord(uint32_t v)
{
    checkSpace(sizeof(v));
    *reinterpret_cast<XdrWord*>(next_) = v;
    next_ += sizeof(v);
    if (next_ == limit_)
	flush(false);
}

void
RecordWriter::putBytes(const uint8_t* p, size_t len)
{
    auto pad = __round(len) - len;
    while (len > 0) {
	if (next_ == limit_)
	    flush(false);
	auto n = len;
	if (n > (limit_ - next_))
	    n = limit_ - next_;
	std::copy_n(p, n, next_);
	p += n;
	next_ += n;
	len -= n;
    }
    while (pad > 0) {
	if (next_ == limit_)
	    flush(false);
	auto n = pad;
	if (n > (limit_ - next_))
	    n = limit_ - next_;
	std::fill_n(next_, n, 0);
	next_ += n;
	pad -= n;
    }
}

void
RecordWriter::checkSpace(size_t len)
{
    if (next_ == limit_)
	flush(false);
    if (next_ + len > limit_)
	throw XdrError("overflow");
}

void
RecordWriter::startNewFragment()
{
    next_ = buf_.data();
    limit_ = next_ + buf_.size();
    uint32_t rec = 0;
    xdr(rec, this);
}

void
RecordWriter::flush(bool endOfRecord)
{
    auto len = next_ - buf_.data();
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
    next_ = limit_ = fragLimit_ = buf_.data();
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
    if (lastFragment_ == true && fragRemaining_ == 0)
	return;
    while (lastFragment_ == false || fragRemaining_ > 0) {
	auto n = fragLimit_ - next_;
	next_ = fragLimit_;
	fragRemaining_ -= n;
	if (lastFragment_ == false || fragRemaining_ > 0)
	    fill();
    }
    lastFragment_ = false;
}

void
RecordReader::endRecord()
{
    assert(lastFragment_ && fragRemaining_ == 0);
    lastFragment_ = false;
}

void
RecordReader::getWord(uint32_t& v)
{
    if (next_ + sizeof(v) <= fragLimit_ && isWordAligned(next_)) {
	v = *reinterpret_cast<const XdrWord*>(next_);
	next_ += sizeof(v);
	fragRemaining_ -= sizeof(v);
    }
    else {
	std::aligned_storage<sizeof(uint32_t), alignof(uint32_t)>::type buf;
	getBytes(reinterpret_cast<uint8_t*>(&buf), sizeof(v));
	v = *reinterpret_cast<const XdrWord*>(&buf);
    }
}

void
RecordReader::getBytes(uint8_t* p, size_t len)
{
#if 0
    debug (rec) {
	writefln("read(minSize=%d, maxSize=%d", minSize, maxSize);
    }
#endif

    auto pad = __round(len) - len;
    while (len > 0) {
	if (next_ == fragLimit_)
	    fill();
	auto n = len;
	if (n > (fragLimit_ - next_))
	    n = fragLimit_ - next_;
	std::copy_n(next_, n, p);
	p += n;
	next_ += n;
	fragRemaining_ -= n;
	len -= n;
    }
    while (pad > 0) {
	if (next_ == fragLimit_)
	    fill();
	auto n = pad;
	if (n > (fragLimit_ - next_))
	    n = fragLimit_ - next_;
	next_ += n;
	fragRemaining_ -= n;
	pad -= n;
    }
}

void
RecordReader::fill()
{
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
	while (next_ + 4 > limit_) {
	    // We need to read more data from upstream. Move what we
	    // have to the beginning of our buffer first.
	    auto n = limit_ - next_;
	    if (n > 0 && next_ != buf_.data())
		std::copy_n(next_, n, buf_.data());
	    next_ = buf_.data();
	    limit_ = next_ + n;

	    n = fill_(limit_, buf_.size() - n);
	    if (n <= 0)
		throw XdrError("end of file");
	    limit_ += n;
	}
    }
    else {
	// Read from upstream
	auto n = fill_(buf_.data(), buf_.size());
	if (n <= 0)
	    throw XdrError("end of file");
	next_ = buf_.data();
	limit_ = next_ + n;
    }
    if (fragRemaining_ == 0) {
	// We refuse to read past the end of the last fragment until
	// our owner tells us we are finished with the whole
	// record. Note that we have already ensured that we at least
	// have a complete fragment header in our buffer.
	if (lastFragment_)
	    throw XdrError("end of record");
	uint32_t rec = *reinterpret_cast<const XdrWord*>(next_);
	next_ += sizeof(rec);
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
    if (next_ + fragRemaining_ < limit_)
	fragLimit_ = next_ + fragRemaining_;
    else
	fragLimit_ = limit_;
}

void
RecordReader::need(size_t sz)
{
#if 0
    debug (rec) {
	writefln("need(sz=%d)", sz);
    }
#endif
    // If we have enough buffered and haven't reached the end of
    // this fragment, we are done.
    if (next_ + sz <= fragLimit_)
	return;
    fill();
    assert(next_ + sz <= fragLimit_);
}
