// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Request validation, response header parsing and framing rules (platform/net/HttpMessage.h).
#include "AetherTest.h"

#include "platform/net/HttpMessage.h"

#include <string>
#include <vector>

using namespace aether;
using namespace aether::net;

AETHER_TEST("http message: methods must be RFC 9110 tokens") {
    CHECK(IsValidMethod("GET"));
    CHECK(IsValidMethod("POST"));
    CHECK(IsValidMethod("PROPFIND"));
    CHECK_FALSE(IsValidMethod(""));
    CHECK_FALSE(IsValidMethod("GE T"));
    CHECK_FALSE(IsValidMethod("GET\r\n"));
    CHECK_FALSE(IsValidMethod("G:ET"));
    CHECK_FALSE(IsValidMethod(std::string(33, 'A')));
}

AETHER_TEST("http message: request headers cannot inject lines or override framing") {
    const std::vector<HttpHeader> good{{"Accept", "application/vnd.github+json"},
                                       {"X-GitHub-Api-Version", "2022-11-28"},
                                       {"X-Tabbed", "a\tb"}};
    CHECK(ValidateRequestHeaders(good).ok());
    CHECK(ValidateRequestHeaders({}).ok());

    const std::vector<HttpHeader> injected{{"Accept", "text/plain\r\nX-Injected: 1"}};
    auto rejected = ValidateRequestHeaders(injected);
    REQUIRE_FALSE(rejected.ok());
    CHECK_EQ(rejected.error().code, ErrorCode::InvalidArgument);
    CHECK(rejected.error().message.find("X-Injected") == std::string::npos);

    // Values are never echoed: they may be credentials.
    const std::vector<HttpHeader> secret{{"Authorization", "Bearer SECRET-TOKEN\n"}};
    auto secretRejected = ValidateRequestHeaders(secret);
    REQUIRE_FALSE(secretRejected.ok());
    CHECK(secretRejected.error().message.find("SECRET-TOKEN") == std::string::npos);
    CHECK(secretRejected.error().message.find("Authorization") != std::string::npos);

    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"Bad Name", "x"}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"", "x"}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"X-Colon:", "x"}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"X-Utf8", "caf\xC3\xA9"}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"X-Nul", std::string("a\0b", 3)}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"Host", "evil.example"}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"content-length", "5"}}).ok());
    CHECK_FALSE(ValidateRequestHeaders(std::vector<HttpHeader>{{"Transfer-Encoding", "chunked"}}).ok());
}

AETHER_TEST("http message: request headers are formatted as CRLF lines") {
    const std::vector<HttpHeader> headers{{"Accept", "x"}, {"X-Padded", "  y  "}};
    CHECK_EQ(FormatRequestHeaders(headers), std::string("Accept: x\r\nX-Padded: y\r\n"));
    CHECK(FormatRequestHeaders({}).empty());
}

AETHER_TEST("http message: raw response headers are parsed in order") {
    const std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "X-Multi: a\r\n"
        "X-Multi:b\r\n"
        "X-Folded: one\r\n"
        "  two\r\n"
        "not a header line\r\n"
        ": no name\r\n"
        "X-Empty:\r\n"
        "\r\n";
    const auto headers = ParseRawHeaders(raw);
    REQUIRE_EQ(headers.size(), std::size_t{5});
    CHECK_EQ(headers[0].name, std::string("Content-Type"));
    CHECK_EQ(headers[0].value, std::string("text/plain; charset=utf-8"));
    CHECK_EQ(headers[1].value, std::string("a"));
    CHECK_EQ(headers[2].name, std::string("X-Multi"));
    CHECK_EQ(headers[2].value, std::string("b"));
    CHECK_EQ(headers[3].value, std::string("one two"));
    CHECK_EQ(headers[4].name, std::string("X-Empty"));
    CHECK(headers[4].value.empty());

    REQUIRE(FindHeader(headers, "content-type") != nullptr);
    CHECK_EQ(*FindHeader(headers, "CONTENT-TYPE"), std::string("text/plain; charset=utf-8"));
    CHECK_EQ(*FindHeader(headers, "x-multi"), std::string("a"));
    CHECK(FindHeader(headers, "Location") == nullptr);

    // HTTP/2 status lines and bare LF line endings are handled too.
    const auto h2 = ParseRawHeaders("HTTP/2 404\nserver: test\n\n");
    REQUIRE_EQ(h2.size(), std::size_t{1});
    CHECK_EQ(h2[0].name, std::string("server"));
    CHECK(ParseRawHeaders("").empty());
}

AETHER_TEST("http message: Content-Length is parsed strictly") {
    using Headers = std::vector<HttpHeader>;
    CHECK_FALSE(ParseContentLength(Headers{}).has_value());
    CHECK_EQ(ParseContentLength(Headers{{"Content-Length", "123"}}).value_or(0), std::uint64_t{123});
    CHECK_EQ(ParseContentLength(Headers{{"content-length", " 42 "}}).value_or(0), std::uint64_t{42});
    CHECK_EQ(ParseContentLength(Headers{{"Content-Length", "0"}}).value_or(1), std::uint64_t{0});
    CHECK_EQ(ParseContentLength(Headers{{"Content-Length", "5"}, {"Content-Length", "5"}}).value_or(0),
             std::uint64_t{5});
    CHECK_FALSE(ParseContentLength(Headers{{"Content-Length", "5"}, {"Content-Length", "6"}}).has_value());
    CHECK_FALSE(ParseContentLength(Headers{{"Content-Length", "-1"}}).has_value());
    CHECK_FALSE(ParseContentLength(Headers{{"Content-Length", "+5"}}).has_value());
    CHECK_FALSE(ParseContentLength(Headers{{"Content-Length", "12a"}}).has_value());
    CHECK_FALSE(ParseContentLength(Headers{{"Content-Length", ""}}).has_value());
    CHECK_FALSE(ParseContentLength(Headers{{"Content-Length", "99999999999999999999999"}}).has_value());
    CHECK_EQ(ParseContentLength(Headers{{"Content-Length", "18446744073709551615"}}).value_or(0),
             std::uint64_t{18446744073709551615ull});
}

AETHER_TEST("http message: content coding, body presence and credential headers") {
    using Headers = std::vector<HttpHeader>;
    // Only a lone gzip or deflate coding is decoded by WinHTTP; everything else passes through with an
    // exact Content-Length (measured with the loopback server, see LoopbackClientTests.cpp).
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{}));
    CHECK(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "gzip"}}));
    CHECK(IsWinHttpDecodedCoding(Headers{{"content-encoding", " GZIP "}}));
    CHECK(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "deflate"}}));
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "br"}}));
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "x-gzip"}}));
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "gzip, br"}}));
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "identity"}}));
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", ""}}));
    CHECK_FALSE(IsWinHttpDecodedCoding(Headers{{"Content-Encoding", "gzip"}, {"Content-Encoding", "br"}}));

    CHECK(ResponseHasBody("GET", 200));
    CHECK(ResponseHasBody("GET", 404));
    CHECK(ResponseHasBody("POST", 500));
    CHECK_FALSE(ResponseHasBody("HEAD", 200));
    CHECK_FALSE(ResponseHasBody("GET", 204));
    CHECK_FALSE(ResponseHasBody("GET", 304));
    CHECK_FALSE(ResponseHasBody("GET", 100));

    CHECK(IsCredentialHeader("Authorization"));
    CHECK(IsCredentialHeader("proxy-authorization"));
    CHECK(IsCredentialHeader("COOKIE"));
    CHECK_FALSE(IsCredentialHeader("Accept"));
    CHECK_FALSE(IsCredentialHeader("X-Authorization-Hint"));
}
