/*-
 * Copyright) 2016 Doug Rabson
 * All rights reserved.
 */

#include <algorithm>
#include <cassert>
#include <cctype>
#include <iomanip>
#include <sstream>

#include <glog/logging.h>

#include <rpc++/errors.h>
#include <rpc++/json.h>
#include <rpc++/rest.h>
#include <rpc++/socket.h>
#include <rpc++/urlparser.h>
#include <rpc++/xml.h>

using namespace oncrpc;

RestRegistry::RestRegistry()
{
}

void RestRegistry::add(
    const std::string& uri, bool exact, std::shared_ptr<RestHandler> handler)
{
    assert(uri[0] == '/');
    UrlParser p(uri);

    auto ep = &root_;
    for (auto& seg: p.segments) {
        ep = &ep->children[seg];
    }
    ep->exact = exact;
    ep->handler = handler;
}

void RestRegistry::add(
    const std::string& uri,
    const std::string& content,
    const std::string& type,
    std::time_t lastModified)
{
    assert(uri[0] == '/');
    UrlParser p(uri);

    auto ep = &root_;
    for (auto& seg: p.segments) {
        ep = &ep->children[seg];
    }
    ep->exact = true;
    ep->content = std::make_unique<staticContent>(
        staticContent{content, type, lastModified});
}

void RestRegistry::remove(const std::string& uri)
{
    assert(uri[0] == '/');
    UrlParser p(uri);

    // Look up each segment of the path first
    std::vector<entry*> path;
    auto ep = &root_;
    path.push_back(ep);
    for (auto& seg: p.segments) {
        auto it = ep->children.find(seg);
        if (it == ep->children.end())
            break;
        ep = &it->second;
        path.push_back(ep);
    }

    // If we didn't manage to look up every segment of the URI, then
    // there is not existing handler. Note that path includes the root
    // entry as well as entries for each matching segment of the URI.
    if (path.size() == p.segments.size() + 1) {
        ep = path.back();
        ep->exact = true;
        ep->handler.reset();

        // Work our way back up the path removing entries that are
        // only in the tree for the URI being removed.
        auto i = p.segments.size() - 1;
        do {
            if (ep->children.size() > 0)
                return;
            path.pop_back();
            ep = path.back();
            auto& seg = p.segments[i];
            i--;

            ep->children.erase(seg);
        } while (path.size() > 1);
    }
}

static auto IMF_FIXDATE_FMT = "%a, %d %b %Y %H:%M:%S GMT";
static auto RFC850_FMT = "%a, %d-%b-%y %H:%M:%S GMT";
static auto ASCTIME_FMT = "%a %b %d %H:%M:%S %Y";

static std::string formatTime(std::time_t t)
{
    struct tm tmbuf;
    auto tm = *gmtime_r(&t, &tmbuf);
    std::ostringstream ss;
    ss << std::put_time(&tm, IMF_FIXDATE_FMT);
    return ss.str();
}

static bool tryParseTime(const std::string& s, const char* fmt, std::time_t& t)
{
    std::istringstream ss(s);
    std::tm tm = {};
    ss >> std::get_time(&tm, fmt);
    if (ss.fail())
        return false;
    t = std::mktime(&tm);
    return true;
}

static std::time_t parseTime(const std::string& s)
{
    std::time_t t;
    if (tryParseTime(s, IMF_FIXDATE_FMT, t))
        return t;
    if (tryParseTime(s, RFC850_FMT, t))
        return t;
    if (tryParseTime(s, ASCTIME_FMT, t))
        return t;
    return 0;
}

std::shared_ptr<RestResponse> RestRegistry::process(
    std::shared_ptr<RestRequest> req)
{
    VLOG(1) << "Servicing REST request: " << req->method()
            << " " << req->uri().all
            << " " << req->protocol()
            << "/" << req->versionMajor()
            << "." << req->versionMinor();


    auto res = std::make_shared<RestResponse>("HTTP", 1, 1);
    res->setAttr("Server", "rpc++/0.1");
    res->setAttr("Content-Length", "0");

    if (filter_) {
        if (!filter_->check(req->address())) {
            VLOG(1) << "Request not authorized from: "
                    << req->address().host();
            res->setStatus(403);
            res->setReason("Forbidden");
            return res;
        }
    }

    if (req->protocol() != "HTTP" ||
        req->versionMajor() != 1 ||
        req->versionMinor() != 1) {
        LOG(ERROR) << "Unsupported REST protocol: " << req->protocol()
                   << "/" << req->versionMajor()
                   << "." << req->versionMinor();
        res->setStatus(505);
        res->setReason("HTTP Version not supported");
        return res;
    }

    auto& attrs = req->attrs();
    auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    res->setAttr("Date", formatTime(now));

    if (attrs.find("Host") == attrs.end()) {
        // In HTTP/1.1, clients MUST include a Host header
        res->setStatus(400);
        res->setReason("Bad Request");
    }
    else if (req->method() != "GET" && req->method() != "POST") {
        res->setStatus(501);
        res->setReason("Not Implemented");
    }
    else {
        // Select the handler with the longest matching prefix
        auto& uri = req->uri();
        auto ep = resolve(uri);
        if (!ep && uri.path == "/") {
            // Try resolving /index.html instead
            ep = resolve(UrlParser("/index.html"));
        }
        if (ep) {
            res->setStatus(200);
            res->setReason("OK");
            if (ep->handler) {
                std::ostringstream ss;
                std::unique_ptr<RestEncoder> enc;
                bool pretty = uri.query.find("pretty") != uri.query.end();
                auto it = uri.query.find("format");
                if (it != uri.query.end()) {
                    if (it->second == "json")
                        enc = std::make_unique<JsonEncoder>(ss, pretty);
                    else if (it->second == "xml")
                        enc = std::make_unique<XmlEncoder>(ss, pretty);
                }
                if (!enc) {
                    enc = std::make_unique<JsonEncoder>(ss, pretty);
                }
                bool handled;
                if (req->method() == "GET")
                    handled = ep->handler->get(req, std::move(enc));
                else
                    handled = ep->handler->post(req, std::move(enc));
                if (handled) {
                    enc.reset();
                    res->setBody(ss.str(), "application/json");
                    return res;
                }
            }
            else if (ep->content) {
                if (req->method() != "GET") {
                    res->setStatus(501);
                    res->setReason("Not Implemented");
                    return res;
                }
                res->setAttr(
                    "Last-Modified", formatTime(ep->content->lastModified));
                auto it = attrs.find("If-Modified-Since");
                if (it != attrs.end()) {
                    auto checkTime = parseTime(it->second);
                    VLOG(1) << "If-Modified-Since: "
                            << "lastModified=" << ep->content->lastModified
                            << ", checkTime=" << checkTime;
                    if (ep->content->lastModified <= checkTime) {
                        VLOG(1) << "Returning 304 - Not Modified";
                        res->setStatus(304);
                        res->setReason("Not Modified");
                        res->setBody("", ep->content->type);
                    }
                }
                it = attrs.find("If-Unmodified-Since");
                if (it != attrs.end()) {
                    auto checkTime = parseTime(it->second);
                    VLOG(1) << "If-Unmodified-Since: "
                            << "lastModified=" << ep->content->lastModified
                            << ", checkTime=" << checkTime;
                    if (ep->content->lastModified >= checkTime) {
                        VLOG(1) << "Returning 412 - Predondition Failed";
                        res->setStatus(412);
                        res->setReason("Precondition Failed");
                        res->setBody("", ep->content->type);
                    }
                }
                // If we are still 200, add the content body to the reply
                if (res->status() == 200) {
                    res->setBody(ep->content->content, ep->content->type);
                }
                return res;
            }
        }
        res->setStatus(404);
        res->setReason("Not Found");
        res->setBody("", "text/plain");
    }
    return res;
}

RestRegistry::entry* RestRegistry::resolve(const UrlParser& uri)
{
    if (uri.path[0] != '/')
        return nullptr;

    auto ep = &root_;

    // Look up each segment of the path first
    std::vector<entry*> path;
    path.push_back(ep);
    for (auto& seg: uri.segments) {
        auto it = ep->children.find(seg);
        if (it == ep->children.end())
            break;
        ep = &it->second;
        path.push_back(ep);
    }

    // For exact matches, only the last entry we looked up should
    // match and then only if we managed to look up every segment of
    // the URI. Note that path includes the root entry as well as
    // entries for each matching segment of the URI.
    ep = path.back();
    if ((ep->handler || ep->content) &&
        path.size() == uri.segments.size() + 1)
        return ep;

    // Now work back up the resolved path to find the closest inexact
    // match.
    while (path.size() > 0) {
        ep = path.back();

        if ((ep->handler || ep->content) && !ep->exact)
            return ep;

        // No match at this level, back up to see if we can find an
        // earlier match
        path.pop_back();
    }

    return nullptr;
}

RestChannel::RestChannel(
    std::shared_ptr<RestRegistry> restreg)
    : restreg_(restreg)
{
}

RestChannel::RestChannel(
    std::shared_ptr<RestRegistry> restreg,
    std::array<char, 4> data)
    : restreg_(restreg)
{
    buffer_.resize(4);
    std::copy_n(data.begin(), 4, buffer_.begin());
}

static inline int fromHexChar(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    throw std::system_error(ENOENT, std::system_category());
}

bool RestChannel::onReadable(Socket* sock)
{
    uint8_t buf[1024];
    auto bytes = sock->recv(buf, sizeof(buf));
    if (bytes == 0)
        return false;
    buffer_.insert(buffer_.end(), buf, buf + bytes);

    // All states except PROCESS need to read from the buffer
    while (buffer_.size() > 0 || state_ == PROCESS) {
        switch (state_) {
        case IDLE: {
            // If we have a complete request, pull it out of the buffer
            // and parse it
            size_t i;
            size_t sz = buffer_.size();
            if (sz < 4)
                return true;
            for (i = 0; i < sz - 3; i++) {
                if (buffer_[i] == '\r' && buffer_[i + 1] == '\n' &&
                    buffer_[i + 2] == '\r' && buffer_[i + 3] == '\n')
                    break;
            }
            if (i == sz - 3)
                return true;

            // We have a \r\n\r\n message delimiter - try to parse the
            // request
            try {
                req_ = std::make_shared<RestRequest>(buffer_);
            }
            catch (RestError& e) {
                LOG(ERROR) << "Error parsing REST message: " << e.what();
                sock->close();
                return false;
            }
            req_->setAddress(sock->peerName());

            if (req_->method() == "POST" || req_->method() == "PUT") {
                auto& attrs = req_->attrs();
                auto it = attrs.find("Content-Length");
                if (it != attrs.end()) {
                    state_ = READ_SIZED_BODY;
                    size_ = std::stoi(it->second);
                }
                else {
                    it = attrs.find("Transfer-Encoding");
                    if (it != attrs.end()) {
                        if (it->second != "chunked") {
                            LOG(ERROR) << "Unsupported transfer encoding: "
                                       << it->second;
                            sock->close();
                            return false;
                        }
                        state_ = CHUNK_SIZE_FIRST;
                        size_ = 0;
                    }
                    else {
                        // XXX should we support EOF delimited data?
                        state_ = PROCESS;
                    }
                }
            }
            else {
                    state_ = PROCESS;
            }
            break;
        }

        case READ_SIZED_BODY: {
            if (buffer_.size() < size_)
                return true;
            req_->setBody(
                std::string(buffer_.begin(), buffer_.begin() + size_));
            buffer_.erase(buffer_.begin(), buffer_.begin() + size_);
            state_ = PROCESS;
            break;
        }

        case CHUNK_SIZE_FIRST: {
            // Chunk size must start with a hex digit
            auto ch = buffer_.front();
            buffer_.pop_front();
            try {
                size_ = fromHexChar(ch);
            }
            catch (std::system_error&) {
                LOG(ERROR) << "Expected hex digit";
                sock->close();
                return false;
            }
            state_ = CHUNK_SIZE;
            break;
        }

        case CHUNK_SIZE: {
            // Read subsequent hex digits until we see something else
            auto ch = buffer_.front();
            buffer_.pop_front();
            try {
                size_ = 16*size_ + fromHexChar(ch);
            }
            catch (std::system_error&) {
                buffer_.push_front(ch);
                state_ = CHUNK_SIZE_CR;
                break;
            }
            break;
        }

        case CHUNK_SIZE_CR: {
            // Skip characters up to CR (i.e. ignore chunk-ext)
            auto ch = buffer_.front();
            buffer_.pop_front();
            if (ch == '\r')
                state_ = CHUNK_SIZE_NL;
            break;
        }

        case CHUNK_SIZE_NL: {
            auto ch = buffer_.front();
            buffer_.pop_front();
            if (ch != '\n') {
                LOG(ERROR) << "Expected CRLF after chunk size";
                sock->close();
                return false;
            }
            if (size_ == 0) {
                // Empty last chunk - should be followed by trailer
                state_ = TRAILER_CR;
            }
            else {
                state_ = CHUNK_BODY;
            }
            break;
        }

        case TRAILER_CR: {
            auto ch = buffer_.front();
            buffer_.pop_front();
            if (ch != '\r') {
                LOG(ERROR) << "Expected CRLF after chunk size";
                sock->close();
                return false;
            }
            state_ = TRAILER_NL;
            break;
        }

        case TRAILER_NL: {
            auto ch = buffer_.front();
            buffer_.pop_front();
            if (ch != '\n') {
                LOG(ERROR) << "Expected CRLF after chunk size";
                sock->close();
                return false;
            }
            state_ = PROCESS;
            break;
        }

        case CHUNK_BODY: {
            if (buffer_.size() < size_)
                return true;
            std::string chunk(buffer_.begin(), buffer_.begin() + size_);
            buffer_.erase(buffer_.begin(), buffer_.begin() + size_);
            req_->setBody(req_->body() + chunk);
            state_ = CHUNK_END_CR;
            break;
        }

        case CHUNK_END_CR: {
            auto ch = buffer_.front();
            buffer_.pop_front();
            if (ch != '\r') {
                LOG(ERROR) << "Expected CRLF after chunk";
                sock->close();
                return false;
            }
            state_ = CHUNK_END_NL;
            break;
        }

        case CHUNK_END_NL: {
            auto ch = buffer_.front();
            buffer_.pop_front();
            if (ch != '\n') {
                LOG(ERROR) << "Expected CRLF after chunk";
                sock->close();
                return false;
            }
            state_ = CHUNK_SIZE_FIRST;
            break;
        }

        case PROCESS: {
            bool needClose = false;
            auto& attrs = req_->attrs();
            auto it = attrs.find("Connection");
            if (it != attrs.end() && it->second == "close")
                needClose = true;

            auto res = restreg_.lock()->process(req_);
            req_.reset();
            state_ = IDLE;

            // Write the response
            std::ostringstream ss;
            res->emit(ss);
            sock->send(&ss.str()[0], ss.str().size());
            if (needClose) {
                sock->close();
                return false;
            }
            break;
        }
        }
    }

    return true;
}

char RestMessage::readChar(std::deque<char>& message)
{
    if (message.size() == 0)
        throw RestError("unexpected end of message");
    char ch = message.front();
    message.pop_front();
    return ch;
}

void RestMessage::readExpected(const std::string& expected, std::deque<char>& message)
{
    for (auto expectedChar: expected) {
        char ch = readChar(message);
        if (expectedChar != ch)
            throw RestError(
                std::string("Expected '") +
                expectedChar +
                std::string("' reading HTTP stream"));
    }
}

void RestMessage::readLWS(std::deque<char>& message)
{
    char ch = readChar(message);

    if (ch == '\r') {
	readExpected("\n", message);
	ch = readChar(message);
    }

    if (ch != ' ' && ch != '\t')
	throw RestError("Expected LWS reading HTTP stream");

    while (ch == ' ' || ch == '\t')
	ch = readChar(message);
    message.push_front(ch);
}

int RestMessage::readDigit(std::deque<char>& message)
{
    char ch = readChar(message);
    if (!std::isdigit(ch))
        throw RestError("Expected digit reading HTTP stream");
    return ch - '0';
}

int RestMessage::readNumber(std::deque<char>& message)
{
    char ch = readChar(message);
    int num = 0;
    if (!std::isdigit(ch))
        throw RestError("Expected digit reading HTTP stream");

    while (std::isdigit(ch)) {
        num = 10*num + ch - '0';
        ch = readChar(message);
    }
    message.push_front(ch);
    return num;
}

std::string RestMessage::readToken(std::deque<char>& message)
{
    char ch = readChar(message);
    std::string tok = "";

    static std::string tokenTerminators = "()<>&,;:\\\"/[]?={} \t";
    while (!std::iscntrl(ch) &&
           tokenTerminators.find(ch) == std::string::npos) {
        tok += ch;
        ch = readChar(message);
    }
    message.push_front(ch);

    if (tok.size() == 0)
        throw RestError("Expected token reading HTTP stream");
    return tok;
}

void RestMessage::readProtocolVersion(std::deque<char>& message)
{
    protocol_ = readToken(message);
    readExpected("/", message);
    major_ = readNumber(message);
    readExpected(".", message);
    minor_ = readNumber(message);
}

void RestMessage::readHeader(bool& more, std::deque<char>& message)
{
    char ch = readChar(message);
    if (ch == '\r' || ch == '\n') {
	if (ch == '\r')
	    readExpected("\n", message); // eat LF
	more = false;		// no more headers
        return;
    }
    message.push_front(ch);

    std::string field, value;
    field = readToken(message);
    readExpected(":", message);
    readLWS(message);

    for (;;) {
        ch = readChar(message);
	if (ch == '\r' || ch == '\n') {
	    if (ch == '\r') {
		readExpected("\n", message); // read the LF of the CRLF
	    }

	    ch = readChar(message); // check for LWS

	    if (ch == ' ' || ch == '\t') {
		while (ch == ' ' || ch == '\t') {
		    ch = readChar(message); // eat LWS
		}
		value += ' ';	// and replace with SP
	    } else {
                message.push_front(ch);
		break;		// no LWS, we are done
	    }
	}
	value += ch;
    }
    VLOG(2) << "read header field: " << field << ", value: " << value;
    attrs_[ci_string(field.c_str())] = value;
    more = true;
    return;
}

void RestMessage::emit(std::ostream& os)
{
    for (auto& entry: attrs_)
        os << std::string(entry.first.c_str()) << ": " << entry.second << "\r\n";
    os << "\r\n";
}

RestRequest::RestRequest(std::deque<char>& message)
{
    readMethod(message);
    readExpected(" ", message);
    readRequestURI(message);
    readExpected(" ", message);
    readProtocolVersion(message);
    readExpected("\r\n", message);
    bool more = true;
    while (more)
        readHeader(more, message);
}

void RestRequest::readMethod(std::deque<char>& message)
{
    method_ = readToken(message);
}

void RestRequest::readRequestURI(std::deque<char>& message)
{
    std::string uri;

    char ch = readChar(message);
    do {
	uri += ch;
        ch = readChar(message);
    } while (ch != ' ');
    uri_.parse(uri);

    message.push_front(ch);
}

void RestRequest::emit(std::ostream& os)
{
    os << method_ << " " << uri_.all << " "
       << protocol_ << "/" << major_ << "." << minor_ << "\r\n";
    RestMessage::emit(os);
}

RestResponse::RestResponse(std::deque<char>& message)
{
    readStatusLine(message);
    bool more = true;
    while (more)
        readHeader(more, message);
}

RestResponse::RestResponse(
    const std::string& proto, int versionMajor, int versionMinor)
{
    protocol_ = proto;
    major_ = versionMajor;
    minor_ = versionMinor;
}

void RestResponse::readStatusLine(std::deque<char>& message)
{
    readProtocolVersion(message);
    readExpected(" ", message);
    readStatusCode(message);
    readExpected(" ", message);
    readReasonPhrase(message);
    readExpected("\r\n", message);
}

void RestResponse::readStatusCode(std::deque<char>& message)
{
    int a, b, c;
    a = readDigit(message);
    b = readDigit(message);
    c = readDigit(message);
    status_ = a * 100 + b * 10 + c;
}

void RestResponse::readReasonPhrase(std::deque<char>& message)
{
    char ch = readChar(message);
    reason_ = "";
    while (ch != '\r') {
        reason_ += ch;
        ch = readChar(message);
    }
    message.push_front(ch);
}

void RestResponse::emit(std::ostream& os)
{
    os << "HTTP/1.1 " << status_ << " " << reason_ << "\r\n";
    RestMessage::emit(os);

    if (body_.size() > 0) {
        assert(std::stoi(attr("Content-Length")) == int(body_.size()));
        os.write(&body_[0], body_.size());
    }
}
