// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// URL validation and redirect policy (platform/net/UrlPolicy.h). These rules are the security
// boundary of every outgoing request, so they are tested without any network access.
#include "AetherTest.h"

#include "platform/net/UrlPolicy.h"

#include <string>
#include <vector>

using namespace aether;
using namespace aether::net;

namespace {

ErrorCode CodeOf(const Result<ParsedUrl>& result) { return result.ok() ? ErrorCode::Unknown : result.error().code; }

}  // namespace

AETHER_TEST("url policy: https URLs are split into host, port and request target") {
    auto parsed = ParseRequestUrl("https://api.github.com/repos/o/r/releases?per_page=5", false);
    REQUIRE(parsed.ok());
    CHECK(parsed->secure);
    CHECK_EQ(parsed->host, std::string("api.github.com"));
    CHECK_EQ(parsed->port, std::uint16_t{443});
    CHECK_EQ(parsed->pathAndQuery, std::string("/repos/o/r/releases?per_page=5"));
    CHECK_EQ(parsed->Origin(), std::string("https://api.github.com"));
    CHECK_EQ(parsed->ConnectHost(), std::string("api.github.com"));

    auto withPort = ParseRequestUrl("https://Example.COM:8443", false);
    REQUIRE(withPort.ok());
    CHECK_EQ(withPort->host, std::string("Example.COM"));
    CHECK_EQ(withPort->port, std::uint16_t{8443});
    CHECK_EQ(withPort->pathAndQuery, std::string("/"));
    CHECK_EQ(withPort->Origin(), std::string("https://Example.COM:8443"));

    auto queryOnly = ParseRequestUrl("https://example.com?x=1", false);
    REQUIRE(queryOnly.ok());
    CHECK_EQ(queryOnly->pathAndQuery, std::string("/?x=1"));

    // Fragments are never sent to the server.
    auto fragment = ParseRequestUrl("https://example.com/a?b=1#section", false);
    REQUIRE(fragment.ok());
    CHECK_EQ(fragment->pathAndQuery, std::string("/a?b=1"));

    // '@' outside the authority is ordinary path data, not user information.
    auto atInPath = ParseRequestUrl("https://example.com/users/@me", false);
    REQUIRE(atInPath.ok());
    CHECK_EQ(atInPath->host, std::string("example.com"));
    CHECK_EQ(atInPath->pathAndQuery, std::string("/users/@me"));
}

AETHER_TEST("url policy: plain http is allowed only for loopback hosts when the request opts in") {
    CHECK_EQ(CodeOf(ParseRequestUrl("http://example.com/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("http://example.com/", true)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("http://127.0.0.1:8080/x", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("http://127.0.0.2/", true)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("http://localhost.example.com/", true)), ErrorCode::InvalidArgument);

    auto loopback = ParseRequestUrl("http://127.0.0.1:8080/x?y", true);
    REQUIRE(loopback.ok());
    CHECK_FALSE(loopback->secure);
    CHECK_EQ(loopback->port, std::uint16_t{8080});
    CHECK_EQ(loopback->pathAndQuery, std::string("/x?y"));
    CHECK_EQ(loopback->Origin(), std::string("http://127.0.0.1:8080"));

    auto localhost = ParseRequestUrl("http://LocalHost/", true);
    REQUIRE(localhost.ok());
    CHECK_EQ(localhost->port, std::uint16_t{80});
    CHECK_EQ(localhost->Origin(), std::string("http://LocalHost"));

    auto ipv6 = ParseRequestUrl("http://[::1]:9000/v6", true);
    REQUIRE(ipv6.ok());
    CHECK_EQ(ipv6->host, std::string("::1"));
    CHECK_EQ(ipv6->ConnectHost(), std::string("[::1]"));
    CHECK_EQ(ipv6->port, std::uint16_t{9000});
    CHECK_EQ(ipv6->Origin(), std::string("http://[::1]:9000"));
}

AETHER_TEST("url policy: user information, empty hosts and unsupported schemes are rejected") {
    CHECK_EQ(CodeOf(ParseRequestUrl("https://user:secret@example.com/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://user@example.com/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://@example.com/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://:@example.com/", false)), ErrorCode::InvalidArgument);
    auto userInfo = ParseRequestUrl("https://user:secret@example.com/", false);
    CHECK(userInfo.error().message.find("user information") != std::string::npos);

    CHECK_EQ(CodeOf(ParseRequestUrl("https:///path", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://:443/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("", false)), ErrorCode::InvalidArgument);

    CHECK_EQ(CodeOf(ParseRequestUrl("ftp://example.com/file", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("file:///C:/Windows/win.ini", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("javascript:alert(1)", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("//example.com/relative", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("example.com/path", false)), ErrorCode::InvalidArgument);
}

AETHER_TEST("url policy: malformed and ambiguous URLs are rejected") {
    CHECK_EQ(CodeOf(ParseRequestUrl("https://exa mple.com/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://example.com/a b", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://example.com/\r\nX-Injected: 1", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://example.com/\x7f", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://good.example\\@evil.example/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://b\xC3\xBC" "cher.example/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://exa%41mple.com/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://example.com:0/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://example.com:65536/", false)), ErrorCode::InvalidArgument);
    CHECK_EQ(CodeOf(ParseRequestUrl("https://example.com:port/", false)), ErrorCode::InvalidArgument);

    std::string longUrl = "https://example.com/";
    longUrl.append(kMaxUrlLength, 'a');
    CHECK_EQ(CodeOf(ParseRequestUrl(longUrl, false)), ErrorCode::InvalidArgument);
}

AETHER_TEST("url policy: error messages never echo the URL") {
    auto rejected = ParseRequestUrl("http://example.com/data?api_key=SECRET123", false);
    REQUIRE_FALSE(rejected.ok());
    CHECK(rejected.error().message.find("SECRET123") == std::string::npos);
    CHECK(rejected.error().message.find("example.com") == std::string::npos);

    auto withUser = ParseRequestUrl("https://admin:hunter2@example.com/", false);
    REQUIRE_FALSE(withUser.ok());
    CHECK(withUser.error().message.find("hunter2") == std::string::npos);
}

AETHER_TEST("url policy: loopback host detection is exact") {
    CHECK(IsLoopbackHost("localhost"));
    CHECK(IsLoopbackHost("LOCALHOST"));
    CHECK(IsLoopbackHost("127.0.0.1"));
    CHECK(IsLoopbackHost("::1"));
    CHECK(IsLoopbackHost("[::1]"));
    CHECK_FALSE(IsLoopbackHost("127.0.0.2"));
    CHECK_FALSE(IsLoopbackHost("0.0.0.0"));
    CHECK_FALSE(IsLoopbackHost("localhost.example.com"));
    CHECK_FALSE(IsLoopbackHost("::ffff:127.0.0.1"));
    CHECK_FALSE(IsLoopbackHost(""));
}

AETHER_TEST("url policy: only 301, 302, 303, 307 and 308 are followed") {
    for (const int status : {301, 302, 303, 307, 308}) CHECK(IsRedirectStatus(status));
    for (const int status : {200, 204, 300, 304, 305, 306, 309, 400, 404, 500}) CHECK_FALSE(IsRedirectStatus(status));
}

AETHER_TEST("url policy: redirect hosts match the allow-list exactly and case-insensitively") {
    const std::vector<std::string> allowed{"objects.githubusercontent.com", "API.GitHub.com", "[::1]"};
    CHECK(IsRedirectHostAllowed("objects.githubusercontent.com", allowed));
    CHECK(IsRedirectHostAllowed("OBJECTS.GitHubUserContent.COM", allowed));
    CHECK(IsRedirectHostAllowed("api.github.com", allowed));
    CHECK(IsRedirectHostAllowed("::1", allowed));
    CHECK(IsRedirectHostAllowed("[::1]", allowed));

    // No suffix, prefix or subdomain matching.
    CHECK_FALSE(IsRedirectHostAllowed("evil.objects.githubusercontent.com", allowed));
    CHECK_FALSE(IsRedirectHostAllowed("objects.githubusercontent.com.evil.example", allowed));
    CHECK_FALSE(IsRedirectHostAllowed("githubusercontent.com", allowed));
    CHECK_FALSE(IsRedirectHostAllowed("objects.githubusercontent.com.", allowed));
    CHECK_FALSE(IsRedirectHostAllowed("github.com", allowed));
    CHECK_FALSE(IsRedirectHostAllowed("", allowed));

    const std::vector<std::string> none;
    CHECK_FALSE(IsRedirectHostAllowed("api.github.com", none));
    const std::vector<std::string> emptyEntry{""};
    CHECK_FALSE(IsRedirectHostAllowed("", emptyEntry));
}

AETHER_TEST("url policy: redirect locations resolve against the current URL (RFC 3986)") {
    const std::string base = "https://a.example/dir/page?q=1";
    const auto resolve = [&](std::string_view reference) {
        auto resolved = ResolveReference(base, reference);
        return resolved.ok() ? resolved.value() : std::string("<error>");
    };
    CHECK_EQ(resolve("https://b.example/x"), std::string("https://b.example/x"));
    CHECK_EQ(resolve("//c.example/y?z"), std::string("https://c.example/y?z"));
    CHECK_EQ(resolve("/root"), std::string("https://a.example/root"));
    CHECK_EQ(resolve("next"), std::string("https://a.example/dir/next"));
    CHECK_EQ(resolve("../up"), std::string("https://a.example/up"));
    CHECK_EQ(resolve("../../../too-far"), std::string("https://a.example/too-far"));
    CHECK_EQ(resolve("./same?k=v"), std::string("https://a.example/dir/same?k=v"));
    CHECK_EQ(resolve("?only=query"), std::string("https://a.example/dir/page?only=query"));
    CHECK_EQ(resolve("#fragment"), std::string("https://a.example/dir/page?q=1"));
    CHECK_EQ(resolve("/a/b/../c/./d"), std::string("https://a.example/a/c/d"));
    CHECK_EQ(resolve("https://b.example/x#frag"), std::string("https://b.example/x"));
    CHECK_EQ(resolve("  /trimmed \r\n"), std::string("https://a.example/trimmed"));

    auto noPathBase = ResolveReference("https://a.example", "rel");
    REQUIRE(noPathBase.ok());
    CHECK_EQ(noPathBase.value(), std::string("https://a.example/rel"));

    CHECK_FALSE(ResolveReference(base, "").ok());
    CHECK_FALSE(ResolveReference(base, "   ").ok());
    CHECK_FALSE(ResolveReference(base, "mailto:someone@example.com").ok());
    CHECK_FALSE(ResolveReference(base, "/bad path").ok());
    CHECK_FALSE(ResolveReference(base, "/x\r\nSet-Cookie: a=b").ok());
    CHECK_FALSE(ResolveReference("not-absolute", "/x").ok());
}

AETHER_TEST("url policy: redirects to allow-listed https hosts are accepted") {
    auto from = ParseRequestUrl("https://api.github.com/repos/o/r/releases/assets/1", false);
    REQUIRE(from.ok());
    const std::vector<std::string> allowed{"objects.githubusercontent.com"};

    auto next = EvaluateRedirect(from.value(), "https://objects.githubusercontent.com/file?sig=abc", allowed, false);
    REQUIRE(next.ok());
    CHECK_EQ(next->host, std::string("objects.githubusercontent.com"));
    CHECK_EQ(next->pathAndQuery, std::string("/file?sig=abc"));

    const std::vector<std::string> mixedCase{"Objects.GitHubUserContent.com"};
    CHECK(EvaluateRedirect(from.value(), "https://OBJECTS.githubusercontent.com/f", mixedCase, false).ok());

    // Loopback test servers may redirect between loopback names over plain http.
    auto loopback = ParseRequestUrl("http://127.0.0.1:1234/start", true);
    REQUIRE(loopback.ok());
    const std::vector<std::string> localhost{"localhost"};
    auto local = EvaluateRedirect(loopback.value(), "http://localhost:1234/ok", localhost, true);
    REQUIRE(local.ok());
    CHECK_EQ(local->host, std::string("localhost"));
    CHECK_FALSE(local->secure);
}

AETHER_TEST("url policy: redirects to untrusted or insecure targets are rejected") {
    auto from = ParseRequestUrl("https://api.github.com/x", false);
    REQUIRE(from.ok());
    const std::vector<std::string> allowed{"objects.githubusercontent.com"};

    auto untrusted = EvaluateRedirect(from.value(), "https://evil.example/payload", allowed, false);
    REQUIRE_FALSE(untrusted.ok());
    CHECK_EQ(untrusted.error().code, ErrorCode::Validation);
    CHECK(untrusted.error().message.find("redirect to untrusted host") != std::string::npos);
    CHECK(untrusted.error().message.find("evil.example") != std::string::npos);
    CHECK(untrusted.error().message.find("/payload") == std::string::npos);

    CHECK_EQ(EvaluateRedirect(from.value(), "https://sub.objects.githubusercontent.com/", allowed, false).error().code,
             ErrorCode::Validation);
    CHECK_EQ(EvaluateRedirect(from.value(), "https://objects.githubusercontent.com.evil.example/", allowed, false)
                 .error()
                 .code,
             ErrorCode::Validation);

    // https -> http is never followed, whatever the allow-list says.
    auto downgrade = EvaluateRedirect(from.value(), "http://objects.githubusercontent.com/f", allowed, false);
    REQUIRE_FALSE(downgrade.ok());
    CHECK_EQ(downgrade.error().code, ErrorCode::Validation);

    auto secureLoopback = ParseRequestUrl("https://localhost/x", true);
    REQUIRE(secureLoopback.ok());
    const std::vector<std::string> localhost{"localhost"};
    auto loopbackDowngrade = EvaluateRedirect(secureLoopback.value(), "http://localhost/y", localhost, true);
    REQUIRE_FALSE(loopbackDowngrade.ok());
    CHECK(loopbackDowngrade.error().message.find("plain http") != std::string::npos);

    // Credentials in the target and non-http schemes are rejected.
    CHECK_EQ(EvaluateRedirect(from.value(), "https://user:pw@objects.githubusercontent.com/", allowed, false)
                 .error()
                 .code,
             ErrorCode::Validation);
    CHECK_EQ(EvaluateRedirect(from.value(), "ftp://objects.githubusercontent.com/", allowed, false).error().code,
             ErrorCode::Validation);

    // An empty allow-list disables redirects entirely.
    const std::vector<std::string> none;
    auto disabled = EvaluateRedirect(from.value(), "https://api.github.com/other", none, false);
    REQUIRE_FALSE(disabled.ok());
    CHECK(disabled.error().message.find("redirect to untrusted host") != std::string::npos);
    CHECK(disabled.error().message.find("allows no redirects") != std::string::npos);
}
