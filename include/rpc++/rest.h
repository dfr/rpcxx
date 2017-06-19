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

#pragma once

#include <array>
#include <deque>
#include <ostream>
#include <map>
#include <memory>
#include <string>

#include <rpc++/socket.h>
#include <rpc++/urlparser.h>

namespace oncrpc {

class Socket;
class RestArrayEncoder;
class RestObjectEncoder;
class RestRequest;
class RestResponse;
class UrlParser;

struct ci_char_traits : public std::char_traits<char> {
    static bool eq(char c1, char c2) {
        return std::toupper(c1) == std::toupper(c2);
    }
    static bool lt(char c1, char c2) {
        return std::toupper(c1) < std::toupper(c2);
    }
    static int compare(const char* s1, const char* s2, size_t n) {
        while ( n-- != 0 ) {
            if ( std::toupper(*s1) < std::toupper(*s2) ) return -1;
            if ( std::toupper(*s1) > std::toupper(*s2) ) return 1;
            ++s1; ++s2;
        }
        return 0;
    }
    static const char* find(const char* s, int n, char a) {
        auto const ua (std::toupper(a));
        while ( n-- != 0 ) {
            if (std::toupper(*s) == ua)
            return s;
            s++;
        }
        return nullptr;
    }
};
 
typedef std::basic_string<char, ci_char_traits> ci_string;

/// Utility classes for encoding REST replies
class RestEncoder
{
public:
    virtual ~RestEncoder() {}

    // Encode a boolean value
    virtual void boolean(bool v) = 0;

    // Encode a numeric value
    virtual void number(int v) = 0;
    virtual void number(long v) = 0;
    virtual void number(float v) = 0;
    virtual void number(double v) = 0;

    // Encode a string
    virtual void string(const std::string& v) = 0;

    // Encode an object - call reset on the pointer to finish
    virtual std::unique_ptr<RestObjectEncoder> object() = 0;

    // Encode an array - call reset on the pointer to finish
    virtual std::unique_ptr<RestArrayEncoder> array() = 0;
};

/// Encode an object with named fields
class RestObjectEncoder
{
public:
    virtual ~RestObjectEncoder() {}

    // Encode an object field - call reset on the pointer to finish
    virtual std::unique_ptr<RestEncoder> field(const char* f) = 0;
};

/// Encode an array
class RestArrayEncoder
{
public:
    virtual ~RestArrayEncoder() {}

    // Encode an array element- call reset on the pointer to finish
    virtual std::unique_ptr<RestEncoder> element() = 0;
};

/// Handle REST methods for a URI prefix
class RestHandler
{
public:
    virtual ~RestHandler() {}

    /// Respond to an http GET request, encoding the reply body in
    /// res. Return true if the request was handled or false otherwise.
    virtual bool get(
        std::shared_ptr<RestRequest> req,
        std::unique_ptr<RestEncoder>&& res)
    {
        return false;
    }

    /// Respond to an http POST request, encoding the reply body in
    /// res. Return true if the request was handled or false otherwise.
    virtual bool post(
        std::shared_ptr<RestRequest> req,
        std::unique_ptr<RestEncoder>&& res)
    {
        return false;
    }
};

/// Register REST handlers and process incoming requests.
class RestRegistry: public std::enable_shared_from_this<RestRegistry>
{
public:
    RestRegistry();

    /// Add a handler to the registry. Each incoming request is
    /// matched segment by segment against the given URI and the
    /// longest match is used to select a handler. If exact is true,
    /// then the handler only matches if the request URI is an exact
    /// match to the handler URI.
    void add(
        const std::string& uri, bool exact,
        std::shared_ptr<RestHandler> handler);

    // Add some static content to the registry which will be returned
    // for requests with the given URI
    void add(
        const std::string& uri,
        const std::string& content,
        const std::string& type,
        std::time_t lastModified);

    /// Remove the handler for the given URI
    void remove(const std::string& uri);

    /// Process a REST message and possibly dispatch to a suitable handler
    std::shared_ptr<RestResponse> process(std::shared_ptr<RestRequest> req);

    /// Add a filter for incoming requests
    void setFilter(std::shared_ptr<Filter> filter)
    {
        filter_ = filter;
    }

private:
    // Small scale static content
    struct staticContent {
        std::string content;
        std::string type;
        std::time_t lastModified;
    };

    // Handlers are registered in a tree structure for efficient
    // lookups
    struct entry {
        bool exact = true;
        std::shared_ptr<RestHandler> handler;
        std::unique_ptr<staticContent> content;
        std::map<const std::string, entry> children;
    };

    /// Find a handler for a given request uri
    entry* resolve(const UrlParser& uri);

    entry root_;
    std::shared_ptr<Filter> filter_;
};

/// Keep track of the channel state for a socket used for REST rpcs
class RestChannel
{
public:
    /// Create a new channel for REST rpc
    RestChannel(
        std::shared_ptr<RestRegistry> restreg);

    /// Create a new channel for REST rpc. This variant is called from
    /// the StreamChannel when it detects an attempt to call a REST
    /// method on a new channel by examining the first four bytes of
    /// data received on the channel.
    RestChannel(
        std::shared_ptr<RestRegistry> restreg, std::array<char, 4> data);

    /// Called when the associated socket has data available to
    /// read. Return true if data was read successfully or false if
    /// end-of-file or some other error was detected.
    bool onReadable(Socket* sock);

private:
    enum State {
        IDLE,
        READ_SIZED_BODY,
        CHUNK_SIZE_FIRST,
        CHUNK_SIZE,
        CHUNK_SIZE_CR,
        CHUNK_SIZE_NL,
        TRAILER_CR,
        TRAILER_NL,
        CHUNK_BODY,
        CHUNK_END_CR,
        CHUNK_END_NL,
        PROCESS
    };

    std::weak_ptr<RestRegistry> restreg_;
    std::deque<char> buffer_;
    std::shared_ptr<RestRequest> req_;
    size_t size_;               // Content-Length or chunk size
    State state_ = IDLE;
};

/// Shared base class for both requests and replies
class RestMessage
{
public:
    auto& attrs() const { return attrs_; }
    auto& protocol() const { return protocol_; }
    auto versionMajor() const { return major_; }
    auto versionMinor() const { return minor_; }
    auto& body() const { return body_; }

    std::string attr(const std::string& attr) {
        return attrs_[ci_string(attr.c_str())];
    }

    void setAttr(const std::string& attr, const std::string& val)
    {
        attrs_[ci_string(attr.c_str())] = val;
    }

    void setBody(const std::string& body)
    {
        body_ = body;
        setAttr("Content-Length", std::to_string(body.size()));
    }

    void setBody(const std::string& body, const std::string& type)
    {
        body_ = body;
        setAttr("Content-Length", std::to_string(body.size()));
        setAttr("Content-Type", type);
    }

    char readChar(std::deque<char>& message);
    void readExpected(const std::string& expected, std::deque<char>& message);
    void readLWS(std::deque<char>& message);
    int readDigit(std::deque<char>& message);
    int readNumber(std::deque<char>& message);
    std::string readToken(std::deque<char>& message);
    void readProtocolVersion(std::deque<char>& message);
    void readHeader(bool& more, std::deque<char>& message);

    void emit(std::ostream& os);

protected:
    std::string protocol_;
    int major_ = 0;
    int minor_ = 0;
    std::map<ci_string, std::string> attrs_;
    std::string body_;
};

/// A REST request
class RestRequest: public RestMessage
{
public:
    RestRequest(std::deque<char>& message);

    auto& method() const { return method_; }
    auto& uri() const { return uri_; }
    auto& address() const { return addr_; }
    void setAddress(const Address& addr)
    {
        addr_ = addr;
    }

    void readMethod(std::deque<char>& message);
    void readRequestURI(std::deque<char>& message);

    void emit(std::ostream& os);

private:
    std::string method_;
    UrlParser uri_;
    Address addr_;
};

/// A REST response
class RestResponse: public RestMessage
{
public:
    RestResponse(std::deque<char>& message);
    RestResponse(
        const std::string& proto, int versionMajor, int versionMinor);

    auto status() const { return status_; }
    auto& reason() const { return reason_; }

    void setStatus(int status) { status_ = status; }
    void setReason(const std::string& reason) { reason_ = reason; }

    void readStatusLine(std::deque<char>& message);
    void readStatusCode(std::deque<char>& message);
    void readReasonPhrase(std::deque<char>& message);

    void emit(std::ostream& os);

private:
    int status_;
    std::string reason_;
};

}
