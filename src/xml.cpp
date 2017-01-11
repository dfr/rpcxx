/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#include <iomanip>
#include <rpc++/xml.h>

using namespace oncrpc;

namespace {

void indent(std::ostream& os, int level)
{
    os << std::string(4*level, ' ');
}

class XmlNestedEncoder: public XmlEncoder
{
public:
    XmlNestedEncoder(
        std::ostream& os, bool pretty, int level, const char* endTag)
        : XmlEncoder(os, pretty, level),
          endTag_(endTag),
          needEndTag_(true)
    {
    }

    ~XmlNestedEncoder() override
    {
        if (needEndTag_) {
            level_--;
            if (pretty_)
                indent(os_, level_);
            os_ << endTag_;
            if (pretty_)
                os_ << std::endl;
        }
    }

    // RestEncoder overrides
    std::unique_ptr<RestObjectEncoder> object() override;
    std::unique_ptr<RestArrayEncoder> array() override;

private:
    const char* endTag_;
    bool needEndTag_ = true;
};

class XmlFieldEncoder: public XmlNestedEncoder
{
public:
    XmlFieldEncoder(
        std::ostream& os, const char* name, bool pretty, int level)
        : XmlNestedEncoder(os, pretty, level, "</field>")
    {
        if (pretty_)
            indent(os_, level_);
        os_ << "<field name=\"" << name << "\">";
        if (pretty_)
            os_ << std::endl;
        level_++;
    }
};

class XmlElementEncoder: public XmlNestedEncoder
{
public:
    XmlElementEncoder(
        std::ostream& os, bool pretty, int level)
        : XmlNestedEncoder(os, pretty, level, "</element>")
    {
        if (pretty_)
            indent(os_, level_);
        os_ << "<element>";
        if (pretty_)
            os_ << std::endl;
        level_++;
    }
};

class XmlObjectEncoder: public RestObjectEncoder
{
public:
    XmlObjectEncoder(
        std::ostream& os, bool pretty, int level, const char* endTag)
        : os_(os),
          pretty_(pretty),
          level_(level),
          endTag_(endTag)
    {
        if (pretty_)
            indent(os_, level_);
        os_ << "<object>";
        if (pretty_)
            os_ << std::endl;
    }
    
    ~XmlObjectEncoder() override
    {
        if (pretty_)
            indent(os_, level_);
        os_ << "</object>";
        if (pretty_)
            os_ << std::endl;
        if (endTag_) {
            level_--;
            if (pretty_)
                indent(os_, level_);
            os_ << endTag_;
            if (pretty_)
                os_ << std::endl;
        }
    }

    std::unique_ptr<RestEncoder> field(const char* name) override
    {
        return std::make_unique<XmlFieldEncoder>(
            os_, name, pretty_, level_ + 1);
    }

private:
    std::ostream& os_;
    bool pretty_;
    int level_;
    const char* endTag_;
};

/// Encode an array
class XmlArrayEncoder: public RestArrayEncoder
{
public:
    XmlArrayEncoder(
        std::ostream& os, bool pretty, int level, const char* endTag)
        : os_(os),
          pretty_(pretty),
          level_(level),
          endTag_(endTag)
    {
        if (pretty_)
            indent(os_, level_);
        os_ << "<array>";
        if (pretty_)
            os_ << std::endl;
    }

    ~XmlArrayEncoder() override
    {
        if (pretty_)
            indent(os_, level_);
        os_ << "</array>";
        if (pretty_)
            os_ << std::endl;
        if (endTag_) {
            level_--;
            if (pretty_)
                indent(os_, level_);
            os_ << endTag_;
            if (pretty_)
                os_ << std::endl;
        }
    }

    std::unique_ptr<RestEncoder> element() override
    {
        return std::make_unique<XmlElementEncoder>(
            os_, pretty_, level_ + 1);
    }

private:
    std::ostream& os_;
    bool pretty_;
    int level_;
    const char* endTag_;
};

std::unique_ptr<RestObjectEncoder> XmlNestedEncoder::object()
{
    needEndTag_ = false;
    return std::make_unique<XmlObjectEncoder>(
        os_, pretty_, level_, endTag_);
}

std::unique_ptr<RestArrayEncoder> XmlNestedEncoder::array()
{
    needEndTag_ = false;
    return std::make_unique<XmlArrayEncoder>(
        os_, pretty_, level_, endTag_);
}

}

XmlEncoder::XmlEncoder(std::ostream& os, bool pretty)
    : XmlEncoder(os, pretty, 0)
{
}


XmlEncoder::XmlEncoder(
    std::ostream& os, bool pretty, int level)
    : os_(os),
      pretty_(pretty),
      level_(level)
{
}

XmlEncoder::~XmlEncoder()
{
}

void XmlEncoder::boolean(bool v)
{
    if (pretty_)
        indent(os_, level_);
    if (v)
        os_ << "<boolean>true</boolean>";
    else
        os_ << "<boolean>false</boolean>";
    if (pretty_)
        os_ << std::endl;
}

void XmlEncoder::number(int v)
{
    if (pretty_)
        indent(os_, level_);
    os_ << "<number>" << v << "</number>";
    if (pretty_)
        os_ << std::endl;
}

void XmlEncoder::number(long v)
{
    if (pretty_)
        indent(os_, level_);
    os_ << "<number>" << v << "</number>";
    if (pretty_)
        os_ << std::endl;
}

void XmlEncoder::number(float v)
{
    if (pretty_)
        indent(os_, level_);
    os_ << "<number>" << v << "</number>";
    if (pretty_)
        os_ << std::endl;
}

void XmlEncoder::number(double v)
{
    if (pretty_)
        indent(os_, level_);
    os_ << "<number>" << v << "</number>";
    if (pretty_)
        os_ << std::endl;
}

void XmlEncoder::string(const std::string& v)
{
    if (pretty_)
        indent(os_, level_);
    // XXX escape string?
    os_ << "<string>" << v << "</string>";
    if (pretty_)
        os_ << std::endl;
}

std::unique_ptr<RestObjectEncoder> XmlEncoder::object()
{
    return std::make_unique<XmlObjectEncoder>(os_, pretty_, level_, nullptr);
}

std::unique_ptr<RestArrayEncoder> XmlEncoder::array()
{
    return std::make_unique<XmlArrayEncoder>(os_, pretty_, level_, nullptr);
}
