/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#pragma once

#include <rpc++/rest.h>

namespace oncrpc {

class JsonEncoder: public RestEncoder
{
public:
    /// Encode a top-level JSON object
    JsonEncoder(std::ostream& os, bool pretty);

    /// Encode a JSON object at the given nesting level
    JsonEncoder(std::ostream& os, bool pretty, int level);

    ~JsonEncoder() override;

    // RestEncoder overrides
    void boolean(bool v) override;
    void number(int v) override;
    void number(long v) override;
    void number(float v) override;
    void number(double v) override;
    void string(const std::string& v) override;
    std::unique_ptr<RestObjectEncoder> object() override;
    std::unique_ptr<RestArrayEncoder> array() override;

private:
    std::ostream& os_;
    bool pretty_;
    int level_;
    bool needNewline_;
};

}
