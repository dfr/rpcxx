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
