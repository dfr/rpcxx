/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <iomanip>
#include <rpc++/json.h>

using namespace oncrpc;

namespace {

void indent(std::ostream& os, int level)
{
    os << std::string(4*level, ' ');
}

class JsonObjectEncoder: public RestObjectEncoder
{
public:
    JsonObjectEncoder(
        std::ostream& os, bool pretty, int level, bool needNewline)
        : os_(os),
          pretty_(pretty),
          level_(level),
          first_(true),
          needNewline_(needNewline)
    {
        os_ << "{";
        if (pretty_)
            os_ << std::endl;
    }
    
    ~JsonObjectEncoder() override
    {
        if (pretty_) {
            if (!first_)
                os_ << std::endl;
            indent(os_, level_);
        }
        os_ << "}";
        if (needNewline_)
            os_ << std::endl;
    }

    std::unique_ptr<RestEncoder> field(const char* f) override
    {
        if (!first_) {
            if (pretty_) {
                os_ << "," << std::endl;
            }
            else {
                os_ << ",";
            }
        }
        if (pretty_) {
            indent(os_, level_ + 1);
        }
        first_ = false;
        os_ << '"' << f << '"' << ":";
        if (pretty_) os_ << " ";
        return std::make_unique<JsonEncoder>(os_, pretty_, level_ + 1);
    }

private:
    std::ostream& os_;
    bool pretty_;
    int level_;
    bool first_;
    bool needNewline_;
};

/// Encode an array
class JsonArrayEncoder: public RestArrayEncoder
{
public:
    JsonArrayEncoder(
        std::ostream& os, bool pretty, int level, bool needNewline)
        : os_(os),
          pretty_(pretty),
          level_(level),
          first_(true),
          needNewline_(needNewline)
    {
        os_ << "[";
        if (pretty_)
            os_ << std::endl;
    }

    ~JsonArrayEncoder() override
    {
        if (pretty_) {
            if (!first_)
                os_ << std::endl;
            indent(os_, level_);
        }
        os_ << "]";
        if (needNewline_)
            os_ << std::endl;
    }

    std::unique_ptr<RestEncoder> element() override
    {
        if (!first_) {
            if (pretty_) {
                os_ << "," << std::endl;
            }
            else {
                os_ << ",";
            }
        }
        if (pretty_) {
            indent(os_, level_ + 1);
        }
        first_ = false;
        return std::make_unique<JsonEncoder>(os_, pretty_, level_ + 1);
    }

private:
    std::ostream& os_;
    bool pretty_;
    int level_;
    bool first_;
    bool needNewline_;
};

}

JsonEncoder::JsonEncoder(std::ostream& os, bool pretty)
    : JsonEncoder(os, pretty, 0)
{
}

JsonEncoder::JsonEncoder(std::ostream& os, bool pretty, int level)
    : os_(os),
      pretty_(pretty),
      level_(level),
      needNewline_(pretty_ && level_ == 0)
{
}

JsonEncoder::~JsonEncoder()
{
    if (needNewline_)
        os_ << std::endl;
}

void JsonEncoder::boolean(bool v)
{
    if (v)
        os_ << "true";
    else
        os_ << "false";
}

void JsonEncoder::number(int v)
{
    os_ << v;
}

void JsonEncoder::number(long v)
{
    os_ << v;
}

void JsonEncoder::number(float v)
{
    os_ << v;
}

void JsonEncoder::number(double v)
{
    os_ << v;
}

void JsonEncoder::string(const std::string& v)
{
    os_ << '"';
    for (auto ch: v) {
        if ((ch >= 0x20 && ch <= 0x21) ||
            (ch >= 0x23 && ch <= 0x5b) ||
            ch >= 0x5d) {
            // unescaped
            os_ << ch;
        }
        else {
            switch (ch) {
            case '"':
            case '\\':
                os_ << '\\' << ch;
                break;
            case '\b':
                os_ << "\\b";
                break;
            case '\f':
                os_ << "\\f";
                break;
            case '\n':
                os_ << "\\n";
                break;
            case '\r':
                os_ << "\\r";
                break;
            case '\t':
                os_ << "\\t";
                break;
            default: {
                auto savefill = os_.fill();
                auto saveflags = os_.flags();
                os_ << "\\u";
                os_ << std::hex << std::setw(4) << std::setfill('0') << int(ch);
                os_.fill(savefill);
                os_.flags(saveflags);
            }
            }
        }
    }
    os_ << '"';
}

std::unique_ptr<RestObjectEncoder> JsonEncoder::object()
{
    bool needNewline = needNewline_;
    needNewline_ = false;
    return std::make_unique<JsonObjectEncoder>(
        os_, pretty_, level_, needNewline);
}

std::unique_ptr<RestArrayEncoder> JsonEncoder::array()
{
    bool needNewline = needNewline_;
    needNewline_ = false;
    return std::make_unique<JsonArrayEncoder>(
        os_, pretty_, level_, needNewline);
}
