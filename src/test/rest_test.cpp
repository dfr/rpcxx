/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
 */

#include <rpc++/channel.h>
#include <rpc++/json.h>
#include <rpc++/rest.h>
#include <rpc++/server.h>
#include <rpc++/xml.h>
#include <gtest/gtest.h>
#include <glog/logging.h>

using namespace oncrpc;
using namespace std;

class MyHandler: public RestHandler
{
public:
    bool get(
        shared_ptr<RestRequest> req,
        unique_ptr<RestEncoder>&& res) override
    {
        auto a = res->array();
        for (int i = 0; i < 5; i++)
            a->element()->number(i);
        a.reset();
        return true;
    }
};

class RestTest: public ::testing::Test
{
public:
    Address makeLocalAddress()
    {
        char tmp[] = "/tmp/rpcTestXXXXX";
        return Address::fromUrl(string("unix://") + mktemp(tmp));
    }

    void unlinkLocalAddress(const Address& addr)
    {
        auto p = reinterpret_cast<const sockaddr_un*>(addr.addr());
        ::unlink(p->sun_path);
    }

    auto request(const std::string& s)
    {
        std::deque<char> message(s.begin(), s.end());
        return make_shared<RestRequest>(message);
    }
};

TEST_F(RestTest, JsonTrue)
{
    ostringstream ss;
    JsonEncoder(ss, false).boolean(true);
    EXPECT_EQ("true", ss.str());
}

TEST_F(RestTest, JsonFalse)
{
    ostringstream ss;
    JsonEncoder(ss, false).boolean(false);
    EXPECT_EQ("false", ss.str());
}

TEST_F(RestTest, JsonInteger)
{
    ostringstream ss;
    JsonEncoder(ss, false).number(1234);
    EXPECT_EQ("1234", ss.str());
}

TEST_F(RestTest, JsonFloat)
{
    ostringstream ss;
    JsonEncoder(ss, false).number(12.34f);
    EXPECT_EQ("12.34", ss.str());
}

TEST_F(RestTest, JsonDouble)
{
    ostringstream ss;
    JsonEncoder(ss, false).number(12.34);
    EXPECT_EQ("12.34", ss.str());
}

TEST_F(RestTest, JsonString)
{
    ostringstream ss;
    JsonEncoder(ss, false).string("test\b\f\n\r\t\001\"\\");
    EXPECT_EQ(R"("test\b\f\n\r\t\u0001\"\\")", ss.str());
}

TEST_F(RestTest, JsonObject)
{
    ostringstream ss;
    auto obj = JsonEncoder(ss, false).object();
    obj->field("foo")->boolean(false);
    obj->field("bar")->number(99);
    obj.reset();
    EXPECT_EQ("{\"foo\":false,\"bar\":99}", ss.str());
}

TEST_F(RestTest, JsonArray)
{
    ostringstream ss;
    auto arr = JsonEncoder(ss, false).array();
    arr->element()->boolean(false);
    arr->element()->number(99);
    arr->element()->object()->field("a")->boolean(true);
    arr.reset();
    EXPECT_EQ("[false,99,{\"a\":true}]", ss.str());
}

TEST_F(RestTest, JsonPretty)
{
    ostringstream ss;
    auto arr = JsonEncoder(ss, true).array();
    arr->element()->boolean(false);
    arr->element()->number(99);
    arr->element()->object()->field("a")->boolean(true);
    arr->element()->array()->element()->number(111);
    arr.reset();
    auto expected =
R"([
    false,
    99,
    {
        "a": true
    },
    [
        111
    ]
]
)";
    EXPECT_EQ(expected, ss.str());
}

TEST_F(RestTest, XmlTrue)
{
    ostringstream ss;
    XmlEncoder(ss, false).boolean(true);
    EXPECT_EQ("<boolean>true</boolean>", ss.str());
}

TEST_F(RestTest, XmlFalse)
{
    ostringstream ss;
    XmlEncoder(ss, false).boolean(false);
    EXPECT_EQ("<boolean>false</boolean>", ss.str());
}

TEST_F(RestTest, XmlInteger)
{
    ostringstream ss;
    XmlEncoder(ss, false).number(1234);
    EXPECT_EQ("<number>1234</number>", ss.str());
}

TEST_F(RestTest, XmlFloat)
{
    ostringstream ss;
    XmlEncoder(ss, false).number(12.34f);
    EXPECT_EQ("<number>12.34</number>", ss.str());
}

TEST_F(RestTest, XmlDouble)
{
    ostringstream ss;
    XmlEncoder(ss, false).number(12.34);
    EXPECT_EQ("<number>12.34</number>", ss.str());
}

TEST_F(RestTest, XmlString)
{
    ostringstream ss;
    XmlEncoder(ss, false).string("test");
    EXPECT_EQ(R"(<string>test</string>)", ss.str());
}

TEST_F(RestTest, XmlObject)
{
    ostringstream ss;
    auto obj = XmlEncoder(ss, false).object();
    obj->field("foo")->boolean(false);
    obj->field("bar")->number(99);
    auto sub = obj->field("baz")->object();
    sub->field("a")->string("b");
    sub.reset();
    obj.reset();
    EXPECT_EQ("<object>"
              "<field name=\"foo\"><boolean>false</boolean></field>"
              "<field name=\"bar\"><number>99</number></field>"
              "<field name=\"baz\">"
              "<object>"
              "<field name=\"a\">"
              "<string>b</string>"
              "</field>"
              "</object>"
              "</field>"
              "</object>", ss.str());
}

TEST_F(RestTest, XmlArray)
{
    ostringstream ss;
    auto arr = XmlEncoder(ss, false).array();
    arr->element()->boolean(false);
    arr->element()->number(99);
    arr->element()->object()->field("a")->boolean(true);
    arr.reset();
    EXPECT_EQ("<array>"
              "<element><boolean>false</boolean></element>"
              "<element><number>99</number></element>"
              "<element>"
              "<object>"
              "<field name=\"a\"><boolean>true</boolean></field>"
              "</object>"
              "</element>"
              "</array>", ss.str());
}

TEST_F(RestTest, XmlPretty)
{
    ostringstream ss;
    auto arr = XmlEncoder(ss, true).array();
    arr->element()->boolean(false);
    arr->element()->number(99);
    arr->element()->object()->field("a")->boolean(true);
    arr->element()->array()->element()->number(111);
    arr.reset();
    auto expected =
R"(<array>
    <element>
        <boolean>false</boolean>
    </element>
    <element>
        <number>99</number>
    </element>
    <element>
        <object>
            <field name="a">
                <boolean>true</boolean>
            </field>
        </object>
    </element>
    <element>
        <array>
            <element>
                <number>111</number>
            </element>
        </array>
    </element>
</array>
)";
    EXPECT_EQ(expected, ss.str());
}

TEST_F(RestTest, ParseRequest)
{
    auto req = request(
        "GET /some/resource HTTP/1.1\r\n"
        "Host: hostname.example.com\r\n"
        "Attr: value\r\n"
        "\r\n");
    EXPECT_EQ("GET", req->method());
    EXPECT_EQ("hostname.example.com", req->attr("Host"));
    EXPECT_EQ("value", req->attr("Attr"));
    EXPECT_EQ(2, req->attrs().size());
}

TEST_F(RestTest, NotFound)
{
    auto restreg = make_shared<RestRegistry>();
    auto req = request(
        "GET /some/resource HTTP/1.1\r\n"
        "Host: hostname.example.com\r\n"
        "Attr: value\r\n"
        "\r\n");
    auto res = restreg->process(req);
    EXPECT_EQ(404, res->status());
}

TEST_F(RestTest, Get)
{
    auto restreg = make_shared<RestRegistry>();
    shared_ptr<RestResponse> res;

    restreg->add("/some/resource", true, make_shared<MyHandler>());
    res = restreg->process(
        request(
            "GET /some/resource HTTP/1.1\r\n"
            "Host: hostname.example.com\r\n"
            "Attr: value\r\n"
            "\r\n"));
    EXPECT_EQ(200, res->status());
    EXPECT_EQ("[0,1,2,3,4]", res->body());

    // Test for pretty response
    res = restreg->process(
        request(
            "GET /some/resource?pretty HTTP/1.1\r\n"
            "Host: hostname.example.com\r\n"
            "Attr: value\r\n"
            "\r\n"));
    EXPECT_EQ(200, res->status());
    EXPECT_EQ(R"([
    0,
    1,
    2,
    3,
    4
]
)", res->body());

    // Test that exact handlers don't match for longer URIs
    res = restreg->process(
        request(
            "GET /some/resource/foo HTTP/1.1\r\n"
            "Host: hostname.example.com\r\n"
            "Attr: value\r\n"
            "\r\n"));
    EXPECT_EQ(404, res->status());

    // Test that we stop matching if the handler is removed
    restreg->remove("/some/resource");
    res = restreg->process(
        request(
            "GET /some/resource HTTP/1.1\r\n"
            "Host: hostname.example.com\r\n"
            "Attr: value\r\n"
            "\r\n"));
    EXPECT_EQ(404, res->status());
}

TEST_F(RestTest, GetStatic)
{
    auto restreg = make_shared<RestRegistry>();
    shared_ptr<RestResponse> res;

    restreg->add("/some/resource", "Hello World!", "text/plain", 0);
                 
    res = restreg->process(
        request(
            "GET /some/resource HTTP/1.1\r\n"
            "Host: hostname.example.com\r\n"
            "Attr: value\r\n"
            "\r\n"));
    EXPECT_EQ(200, res->status());
    EXPECT_EQ("Hello World!", res->body());
}

TEST_F(RestTest, ReceiveRest)
{
    // Test receiving a REST message on an RPC channel
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    auto svcreg = make_shared<ServiceRegistry>();
    auto restreg = make_shared<RestRegistry>();

    // Make a local socket to listen on
    SocketManager sockman;
    auto ssock = make_shared<StreamChannel>(sockpair[0], svcreg, restreg);
    sockman.add(ssock);

    // Process the server side on a thread
    thread t([&]() { sockman.run(); });

    auto chan = make_shared<Socket>(sockpair[1]);
    string message =
        "GET /some/resource HTTP/1.1\r\n"
        "Host: hostname.example.com\r\n"
        "Attr: value\r\n"
        "\r\n";
    chan->send(&message[0], message.size());

    char buf[1024];
    auto n = chan->recv(buf, sizeof(buf));
    chan->close();
    std::deque<char> reply(&buf[0], &buf[n]);
    auto res = make_shared<RestResponse>(reply);
    EXPECT_EQ(404, res->status());
    EXPECT_EQ("Not Found", res->reason());

    sockman.stop();
    t.join();
}

TEST_F(RestTest, PostContentLength)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    auto svcreg = make_shared<ServiceRegistry>();
    auto restreg = make_shared<RestRegistry>();

    // Make a local socket to listen on
    SocketManager sockman;
    auto ssock = make_shared<StreamChannel>(sockpair[0], svcreg, restreg);
    sockman.add(ssock);

    // Process the server side on a thread
    thread t([&]() { sockman.run(); });

    auto chan = make_shared<Socket>(sockpair[1]);
    string message =
        "POST /some/resource HTTP/1.1\r\n"
        "Host: hostname.example.com\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "foo";
    chan->send(&message[0], message.size());

    char buf[1024];
    auto n = chan->recv(buf, sizeof(buf));
    chan->close();
    std::deque<char> reply(&buf[0], &buf[n]);
    auto res = make_shared<RestResponse>(reply);
    EXPECT_EQ(404, res->status());
    EXPECT_EQ("Not Found", res->reason());

    sockman.stop();
    t.join();
}

TEST_F(RestTest, PostChunked)
{
    int sockpair[2];
    ASSERT_GE(::socketpair(AF_LOCAL, SOCK_STREAM, 0, sockpair), 0);

    auto svcreg = make_shared<ServiceRegistry>();
    auto restreg = make_shared<RestRegistry>();

    // Make a local socket to listen on
    SocketManager sockman;
    auto ssock = make_shared<StreamChannel>(sockpair[0], svcreg, restreg);
    sockman.add(ssock);

    // Process the server side on a thread
    thread t([&]() { sockman.run(); });

    auto chan = make_shared<Socket>(sockpair[1]);
    string message =
        "POST /some/resource HTTP/1.1\r\n"
        "Host: hostname.example.com\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "3\r\n"                 // chunk-size
        "foo"                   // chunk
        "\r\n"                  // CRLF after chunk
        "3\r\n"                 // chunk-size
        "bar"                   // chunk
        "\r\n"                  // CRLF after chunk
        "0\r\n"                 // last-chunk
        "\r\n";                 // trailer-part

    chan->send(&message[0], message.size());

    char buf[1024];
    auto n = chan->recv(buf, sizeof(buf));
    chan->close();
    std::deque<char> reply(&buf[0], &buf[n]);
    auto res = make_shared<RestResponse>(reply);
    EXPECT_EQ(404, res->status());
    EXPECT_EQ("Not Found", res->reason());

    sockman.stop();
    t.join();
}
