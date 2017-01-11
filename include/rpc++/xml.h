/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
 */

#pragma once

#include <rpc++/rest.h>

namespace oncrpc {

class XmlEncoder: public RestEncoder
{
public:
    /// Encode a top-level XML object
    XmlEncoder(std::ostream& os, bool pretty);

    /// Encode an XML object at the given nesting level
    XmlEncoder(std::ostream& os, bool pretty, int level);

    ~XmlEncoder() override;

    // RestEncoder overrides
    void boolean(bool v) override;
    void number(int v) override;
    void number(long v) override;
    void number(float v) override;
    void number(double v) override;
    void string(const std::string& v) override;
    std::unique_ptr<RestObjectEncoder> object() override;
    std::unique_ptr<RestArrayEncoder> array() override;

protected:
    std::ostream& os_;
    bool pretty_;
    int level_;
};

}
