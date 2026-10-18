// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// WinHttpClient end to end against the in-process loopback server (LoopbackHttpServer.h): real
// sockets and the real WinHTTP stack, without Internet access. Requests use plain http to
// 127.0.0.1 / localhost / ::1 with HttpRequest::allowInsecureLoopback, the only case where the
// client accepts http.
#include "AetherTest.h"

#include "LoopbackHttpServer.h"
#include "platform/net/WinHttpClient.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace aether;
using namespace aether::net;
using namespace aether::net::testing;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kStreamSize = 200'000;         // /stream: 3 full 64 KB chunks and a 3392-byte tail
constexpr std::size_t kChunkedStreamSize = 150'000;  // /stream-chunked
constexpr std::size_t kStallPrefix = 128 * 1024;     // bytes /stall-body sends before stalling
constexpr std::size_t kUndecodedSize = 100'000;      // /br body (a coding WinHTTP passes through)
// Cancel() may be called from the UI thread, so the cancellation callback must return at once.
constexpr auto kMaxCancelCallDuration = 500ms;

// Deterministic, non-repeating-looking body so misplaced or dropped bytes are detected.
std::string Pattern(std::size_t size) {
    std::string bytes(size, '\0');
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return bytes;
}

// gzip.compress(b"decoded by WinHTTP", compresslevel=9, mtime=0) (Python 3).
std::string GzipHello() {
    static constexpr std::array<unsigned char, 38> kBytes{
        0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xFF, 0x4B, 0x49, 0x4D, 0xCE, 0x4F, 0x49, 0x4D, 0x51, 0x48,
        0xAA, 0x54, 0x08, 0xCF, 0xCC, 0xF3, 0x08, 0x09, 0x09, 0x00, 0x00, 0x45, 0xD9, 0x2F, 0x3E, 0x12, 0x00, 0x00, 0x00};
    return std::string(reinterpret_cast<const char*>(kBytes.data()), kBytes.size());
}

// gzip.compress(b"a" * 262144, compresslevel=9, mtime=0): 290 bytes that decode to 256 KiB. The middle
// of the deflate stream is a run of 253 zero bytes, so it is stored as prefix + zeros + suffix.
std::string GzipBomb() {
    static constexpr std::array<unsigned char, 27> kPrefix{0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                                                           0xFF, 0xED, 0xC1, 0x31, 0x01, 0x00, 0x00, 0x00, 0xC2,
                                                           0xA0, 0xAC, 0xEB, 0x5F, 0xC2, 0xDB, 0x0E, 0x40, 0x01};
    static constexpr std::array<unsigned char, 10> kSuffix{0xBC, 0x01, 0xC4, 0x8D, 0x8D, 0xBA, 0x00, 0x00, 0x04, 0x00};
    std::string bytes(reinterpret_cast<const char*>(kPrefix.data()), kPrefix.size());
    bytes.append(253, '\0');
    bytes.append(reinterpret_cast<const char*>(kSuffix.data()), kSuffix.size());
    return bytes;
}
constexpr std::size_t kGzipBombDecodedSize = 262'144;

// Streams `total` bytes of `fill` in `piece`-sized writes until done or the client goes away.
void StreamBytes(ResponseWriter& out, std::size_t total, std::size_t piece, bool chunked) {
    const std::string block(piece, 'z');
    for (std::size_t sent = 0; sent < total;) {
        const std::size_t size = std::min(piece, total - sent);
        const std::string_view data(block.data(), size);
        if (!(chunked ? out.WriteChunk(data) : out.Write(data))) return;
        sent += size;
    }
    if (chunked) out.FinishChunks();
}

void AddRoutes(LoopbackHttpServer& server) {
    server.Route("/ok", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(200, "OK", "hello from loopback",
                          {{"Content-Type", "text/plain; charset=utf-8"},
                           {"X-Test", "one"},
                           {"X-Multi", "first"},
                           {"X-Multi", "second"}});
    });
    server.Route("/missing", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(404, "Not Found", "not here", {{"Content-Type", "text/plain"}});
    });
    server.Route("/no-content", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteHead(204, "No Content", {{"X-Test", "empty"}});
    });
    server.Route("/echo", [](const RecordedRequest& request, ResponseWriter& out) {
        out.WriteResponse(200, "OK", request.method + "\n" + request.body);
    });

    // Size limits.
    server.Route("/declared-too-large", [](const RecordedRequest&, ResponseWriter& out) {
        if (!out.WriteHead(200, "OK", {}, 10ull * 1024 * 1024)) return;
        StreamBytes(out, 64 * 1024, 16 * 1024, false);
        out.WaitForDisconnect(10s);
    });
    server.Route("/chunked-too-large", [](const RecordedRequest&, ResponseWriter& out) {
        if (!out.WriteHead(200, "OK", {{"Transfer-Encoding", "chunked"}})) return;
        StreamBytes(out, 16 * 1024 * 1024, 16 * 1024, true);
    });
    server.Route("/close-delimited-too-large", [](const RecordedRequest&, ResponseWriter& out) {
        // Neither Content-Length nor chunked: the body ends when the server closes the connection.
        if (!out.WriteHead(200, "OK")) return;
        StreamBytes(out, 1024 * 1024, 16 * 1024, false);
    });
    server.Route("/truncated", [](const RecordedRequest&, ResponseWriter& out) {
        if (!out.WriteHead(200, "OK", {}, 1000)) return;
        out.Write(std::string(500, 't'));  // then the connection closes early
    });
    // A coding WinHTTP does not decode ("br"): the bytes pass through and Content-Length is exact.
    server.Route("/br", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(200, "OK", Pattern(kUndecodedSize), {{"Content-Encoding", "br"}});
    });
    server.Route("/br-truncated", [](const RecordedRequest&, ResponseWriter& out) {
        if (!out.WriteHead(200, "OK", {{"Content-Encoding", "br"}}, kUndecodedSize)) return;
        out.Write(Pattern(kUndecodedSize / 2));  // then the connection closes early
    });
    // Both Content-Length and chunked framing (RFC 9112 section 6.3: handle as an error).
    server.Route("/conflicting-framing", [](const RecordedRequest&, ResponseWriter& out) {
        if (!out.WriteHead(200, "OK", {{"Transfer-Encoding", "chunked"}}, 5)) return;
        out.WriteChunk("0123456789");
        out.FinishChunks();
    });

    // Redirects. The two loopback names reach the same server but are different hosts for the policy.
    server.Route("/redirect-to-localhost", [&server](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(302, "Found", "", {{"Location", server.Url("/ok", "localhost")}});
    });
    server.Route("/redirect-to-127", [&server](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(302, "Found", "", {{"Location", server.Url("/ok", "127.0.0.1")}});
    });
    server.Route("/redirect-relative", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(301, "Moved Permanently", "", {{"Location", "/ok"}});
    });
    server.Route("/redirect-no-location", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(302, "Found", "");
    });
    server.Route("/loop", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(302, "Found", "", {{"Location", "/loop"}});
    });
    server.Route("/see-other", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(303, "See Other", "", {{"Location", "/echo"}});
    });
    server.Route("/temporary", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(307, "Temporary Redirect", "", {{"Location", "/echo"}});
    });
    server.Route("/permanent", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(308, "Permanent Redirect", "", {{"Location", "/echo"}});
    });

    // Stalls (timeouts and cancellation).
    server.Route("/stall-headers", [](const RecordedRequest&, ResponseWriter& out) { out.WaitForDisconnect(60s); });
    server.Route("/stall-body", [](const RecordedRequest&, ResponseWriter& out) {
        if (!out.WriteHead(200, "OK", {}, 1'000'000)) return;
        if (!out.Write(Pattern(kStallPrefix))) return;
        out.WaitForDisconnect(60s);
    });

    // Streaming.
    server.Route("/stream", [](const RecordedRequest&, ResponseWriter& out) {
        const std::string body = Pattern(kStreamSize);
        if (!out.WriteHead(200, "OK", {{"Content-Type", "application/octet-stream"}}, body.size())) return;
        // Odd-sized writes so the client's reads never line up with its 64 KB chunks.
        for (std::size_t offset = 0; offset < body.size(); offset += 10'000) {
            if (!out.Write(std::string_view(body).substr(offset, 10'000))) return;
        }
    });
    server.Route("/stream-chunked", [](const RecordedRequest&, ResponseWriter& out) {
        const std::string body = Pattern(kChunkedStreamSize);
        if (!out.WriteHead(200, "OK", {{"Transfer-Encoding", "chunked"}})) return;
        for (std::size_t offset = 0; offset < body.size(); offset += 7'000) {
            if (!out.WriteChunk(std::string_view(body).substr(offset, 7'000))) return;
        }
        out.FinishChunks();
    });

    // Content coding.
    server.Route("/gzip", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(200, "OK", GzipHello(), {{"Content-Encoding", "gzip"}, {"Content-Type", "text/plain"}});
    });
    server.Route("/gzip-bomb", [](const RecordedRequest&, ResponseWriter& out) {
        out.WriteResponse(200, "OK", GzipBomb(), {{"Content-Encoding", "gzip"}});
    });
}

struct Fixture {
    LoopbackHttpServer server;
    std::unique_ptr<WinHttpClient> client;
    bool ready = false;

    Fixture() {
        AddRoutes(server);
        if (!server.Start()) {
            TEST_NOTE("the loopback server could not start");
            return;
        }
        auto created = WinHttpClient::Create();
        if (!created.ok()) {
            TEST_NOTE("WinHttpClient::Create failed: " + created.error().Describe());
            return;
        }
        client = std::move(created).value();
        ready = true;
    }

    [[nodiscard]] HttpRequest Request(std::string_view path, std::string_view host = "127.0.0.1") const {
        HttpRequest request;
        request.url = server.Url(path, host);
        request.allowInsecureLoopback = true;
        request.connectTimeout = 10s;
        request.receiveTimeout = 15s;
        return request;
    }
};

std::string Text(const Result<HttpResponse>& result) {
    return result.ok() ? "HTTP " + std::to_string(result->status) : result.error().Describe();
}

bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// Header value or "<absent>", so a missing header fails the comparison instead of crashing.
std::string HeaderOf(const HttpResponse& response, std::string_view name) {
    const std::string* value = response.FindHeader(name);
    return value ? *value : std::string("<absent>");
}
std::string HeaderOf(const RecordedRequest& request, std::string_view name) {
    const std::string* value = request.FindHeader(name);
    return value ? *value : std::string("<absent>");
}

template <class Rep, class Period>
long long Ms(std::chrono::duration<Rep, Period> duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

}  // namespace

// ---- Basic exchanges -------------------------------------------------------------------------

AETHER_TEST("loopback: 200 returns the body, status, headers and final URL") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/ok?x=1");
    request.headers.push_back({"X-Client", "aether-tests"});
    request.headers.push_back({"Accept", "text/plain"});
    auto response = f.client->Send(request, {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    CHECK_EQ(response->body, std::string("hello from loopback"));
    CHECK_EQ(response->finalUrl, request.url);
    REQUIRE(response->FindHeader("x-test") != nullptr);
    CHECK_EQ(HeaderOf(*response, "X-Test"), std::string("one"));
    REQUIRE(response->FindHeader("Content-Type") != nullptr);
    CHECK_EQ(HeaderOf(*response, "content-type"), std::string("text/plain; charset=utf-8"));
    CHECK_EQ(HeaderOf(*response, "Content-Length"), std::string("19"));

    // Repeated headers are kept, in order.
    std::vector<std::string> multi;
    for (const auto& header : response->headers) {
        if (header.name == "X-Multi") multi.push_back(header.value);
    }
    REQUIRE_EQ(multi.size(), std::size_t{2});
    CHECK_EQ(multi[0], std::string("first"));
    CHECK_EQ(multi[1], std::string("second"));

    // What the server actually received.
    const auto received = f.server.RequestsTo("/ok");
    REQUIRE_EQ(received.size(), std::size_t{1});
    CHECK_EQ(received[0].method, std::string("GET"));
    CHECK_EQ(received[0].target, std::string("/ok?x=1"));
    REQUIRE(received[0].FindHeader("User-Agent") != nullptr);
    CHECK_EQ(HeaderOf(received[0], "User-Agent"), DefaultUserAgent());
    REQUIRE(received[0].FindHeader("X-Client") != nullptr);
    CHECK_EQ(HeaderOf(received[0], "X-Client"), std::string("aether-tests"));
    REQUIRE(received[0].FindHeader("Host") != nullptr);
    CHECK_EQ(HeaderOf(received[0], "Host"), "127.0.0.1:" + std::to_string(f.server.Port()));
    CHECK(received[0].FindHeader("Cookie") == nullptr);
}

AETHER_TEST("loopback: 404 is a successful result carrying status 404") {
    Fixture f;
    REQUIRE(f.ready);
    auto response = f.client->Send(f.Request("/missing"), {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 404);
    CHECK_EQ(response->body, std::string("not here"));

    auto unrouted = f.client->Send(f.Request("/nothing-here"), {});
    REQUIRE(unrouted.ok());
    CHECK_EQ(unrouted->status, 404);

    auto empty = f.client->Send(f.Request("/no-content"), {});
    REQUIRE(empty.ok());
    CHECK_EQ(empty->status, 204);
    CHECK(empty->body.empty());
}

AETHER_TEST("loopback: POST sends the request body") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/echo");
    request.method = "POST";
    request.body = "payload \xE2\x9C\x93";
    request.headers.push_back({"Content-Type", "text/plain; charset=utf-8"});
    auto response = f.client->Send(request, {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    CHECK_EQ(response->body, "POST\n" + request.body);
    const auto received = f.server.RequestsTo("/echo");
    REQUIRE_EQ(received.size(), std::size_t{1});
    REQUIRE(received[0].FindHeader("Content-Length") != nullptr);
    CHECK_EQ(HeaderOf(received[0], "Content-Length"), std::to_string(request.body.size()));
}

AETHER_TEST("loopback: IPv6 literal URLs reach the server over ::1") {
    Fixture f;
    REQUIRE(f.ready);
    if (!f.server.HasIpv6()) {
        TEST_NOTE("IPv6 loopback is unavailable on this machine; skipped");
        return;
    }
    auto request = f.Request("/ok", "::1");
    CHECK(Contains(request.url, "http://[::1]:"));
    auto response = f.client->Send(request, {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    const auto received = f.server.RequestsTo("/ok");
    REQUIRE_EQ(received.size(), std::size_t{1});
    CHECK(received[0].overIpv6);
}

// ---- Size limits -----------------------------------------------------------------------------

AETHER_TEST("loopback: a declared Content-Length above the limit fails before reading") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/declared-too-large");
    request.maxResponseBytes = 1024 * 1024;
    auto response = f.client->Send(request, {});
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Integrity);
    CHECK(Contains(response.error().message, "declares 10485760 bytes"));

    std::size_t delivered = 0;
    int progressCalls = 0;
    auto streamed = f.client->SendStreaming(
        request,
        [&](std::span<const std::byte> chunk) -> Result<void> {
            delivered += chunk.size();
            return Ok();
        },
        [&](std::uint64_t, std::optional<std::uint64_t>) { ++progressCalls; }, {});
    REQUIRE_FALSE(streamed.ok());
    CHECK_EQ(streamed.error().code, ErrorCode::Integrity);
    CHECK_EQ(delivered, std::size_t{0});
    CHECK_EQ(progressCalls, 0);
}

AETHER_TEST("loopback: a chunked body above the limit aborts with Integrity") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/chunked-too-large");
    request.maxResponseBytes = 100 * 1024;
    auto response = f.client->Send(request, {});
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Integrity);
    CHECK(Contains(response.error().message, "exceeds the limit of 102400 bytes"));

    std::size_t delivered = 0;
    auto streamed = f.client->SendStreaming(
        request,
        [&](std::span<const std::byte> chunk) -> Result<void> {
            delivered += chunk.size();
            return Ok();
        },
        {}, {});
    REQUIRE_FALSE(streamed.ok());
    CHECK_EQ(streamed.error().code, ErrorCode::Integrity);
    CHECK(delivered <= request.maxResponseBytes);
    // The client hung up instead of downloading the whole megabyte.
    CHECK(f.server.WaitForDisconnects(2, 5s));
}

AETHER_TEST("loopback: a close-delimited body above the limit aborts with Integrity") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/close-delimited-too-large");
    request.maxResponseBytes = 100 * 1024;
    auto response = f.client->Send(request, {});
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Integrity);

    // Under the limit the same framing succeeds and returns every byte.
    request.maxResponseBytes = 2 * 1024 * 1024;
    auto complete = f.client->Send(request, {});
    REQUIRE(complete.ok());
    CHECK_EQ(complete->body.size(), std::size_t{1024 * 1024});
}

AETHER_TEST("loopback: a body shorter than its Content-Length is an error, not a short success") {
    Fixture f;
    REQUIRE(f.ready);
    auto response = f.client->Send(f.Request("/truncated"), {});
    TEST_NOTE("truncated body: " + Text(response));
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Network);
}

AETHER_TEST("loopback: a coding WinHTTP does not decode keeps exact length checks and progress totals") {
    Fixture f;
    REQUIRE(f.ready);
    // Regression: a Content-Encoding other than identity used to switch off the truncation check, so a
    // "br" body cut in half came back as a successful 200 with half the bytes.
    auto truncated = f.client->Send(f.Request("/br-truncated"), {});
    TEST_NOTE("truncated br body: " + Text(truncated));
    REQUIRE_FALSE(truncated.ok());
    CHECK_EQ(truncated.error().code, ErrorCode::Network);
    CHECK(Contains(truncated.error().message, "closed after 50000 of 100000 bytes"));

    std::optional<std::uint64_t> lastTotal;
    std::uint64_t lastReceived = 0;
    std::string received;
    auto complete = f.client->SendStreaming(
        f.Request("/br"),
        [&](std::span<const std::byte> chunk) -> Result<void> {
            received.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            return Ok();
        },
        [&](std::uint64_t bytes, std::optional<std::uint64_t> total) {
            lastReceived = bytes;
            lastTotal = total;
        },
        {});
    REQUIRE(complete.ok());
    CHECK_EQ(HeaderOf(*complete, "Content-Encoding"), std::string("br"));
    CHECK(received == Pattern(kUndecodedSize));  // passed through untouched
    CHECK_EQ(lastReceived, std::uint64_t{kUndecodedSize});
    CHECK(lastTotal == std::optional<std::uint64_t>(kUndecodedSize));
}

AETHER_TEST("loopback: a body longer than its Content-Length (conflicting framing) is an error") {
    Fixture f;
    REQUIRE(f.ready);
    auto response = f.client->Send(f.Request("/conflicting-framing"), {});
    TEST_NOTE("conflicting framing: " + Text(response));
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Network);
    CHECK(Contains(response.error().message, "more body bytes than its Content-Length of 5"));
}

// ---- Redirects -------------------------------------------------------------------------------

AETHER_TEST("loopback: a redirect to an allow-listed host is followed and reports finalUrl") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/redirect-to-localhost");
    request.allowedRedirectHosts = {"LOCALHOST"};
    request.headers.push_back({"Authorization", "Bearer loopback-test-token"});
    request.headers.push_back({"X-Client", "aether-tests"});
    auto response = f.client->Send(request, {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    CHECK_EQ(response->body, std::string("hello from loopback"));
    CHECK_EQ(response->finalUrl, f.server.Url("/ok", "localhost"));

    const auto received = f.server.RequestsTo("/ok");
    REQUIRE_EQ(received.size(), std::size_t{1});
    CHECK_EQ(HeaderOf(received[0], "Host"), "localhost:" + std::to_string(f.server.Port()));
    // The origin changed, so the credential was not forwarded; ordinary headers were.
    CHECK(received[0].FindHeader("Authorization") == nullptr);
    CHECK(received[0].FindHeader("X-Client") != nullptr);
}

AETHER_TEST("loopback: a redirect to a host outside the allow-list is rejected") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/redirect-to-localhost");
    request.allowedRedirectHosts = {"127.0.0.1"};
    auto response = f.client->Send(request, {});
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Validation);
    CHECK(Contains(response.error().message, "redirect to untrusted host 'localhost'"));
    CHECK_EQ(response.error().platformCode, std::int64_t{302});

    // An empty allow-list disables redirects, even back to the same host.
    auto sameHost = f.Request("/redirect-to-127");
    auto disabled = f.client->Send(sameHost, {});
    REQUIRE_FALSE(disabled.ok());
    CHECK_EQ(disabled.error().code, ErrorCode::Validation);
    CHECK(Contains(disabled.error().message, "redirect to untrusted host"));

    // The rejected targets were never contacted.
    CHECK_EQ(f.server.CountRequests("/ok"), std::size_t{0});
}

AETHER_TEST("loopback: relative same-origin redirects keep credentials") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/redirect-relative");
    request.allowedRedirectHosts = {"127.0.0.1"};
    request.headers.push_back({"Authorization", "Bearer loopback-test-token"});
    auto response = f.client->Send(request, {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    CHECK_EQ(response->finalUrl, f.server.Url("/ok"));
    const auto received = f.server.RequestsTo("/ok");
    REQUIRE_EQ(received.size(), std::size_t{1});
    REQUIRE(received[0].FindHeader("Authorization") != nullptr);
    CHECK_EQ(HeaderOf(received[0], "Authorization"), std::string("Bearer loopback-test-token"));
}

AETHER_TEST("loopback: redirect loops stop at maxRedirects") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/loop");
    request.allowedRedirectHosts = {"127.0.0.1"};
    request.maxRedirects = 3;
    auto response = f.client->Send(request, {});
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Http);
    CHECK(Contains(response.error().message, "too many redirects (limit 3)"));
    CHECK_EQ(f.server.CountRequests("/loop"), std::size_t{4});  // the original request and 3 redirects

    request.maxRedirects = 0;
    auto none = f.client->Send(request, {});
    REQUIRE_FALSE(none.ok());
    CHECK_EQ(none.error().code, ErrorCode::Http);
    CHECK_EQ(f.server.CountRequests("/loop"), std::size_t{5});

    auto noLocation = f.Request("/redirect-no-location");
    noLocation.allowedRedirectHosts = {"127.0.0.1"};
    auto missing = f.client->Send(noLocation, {});
    REQUIRE_FALSE(missing.ok());
    CHECK_EQ(missing.error().code, ErrorCode::Http);
    CHECK(Contains(missing.error().message, "no Location header"));
}

AETHER_TEST("loopback: 303 switches to GET, 307 and 308 keep the method and body") {
    Fixture f;
    REQUIRE(f.ready);
    const auto post = [&](std::string_view path) {
        auto request = f.Request(path);
        request.method = "POST";
        request.body = "form=1";
        request.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
        request.allowedRedirectHosts = {"127.0.0.1"};
        return f.client->Send(request, {});
    };

    auto seeOther = post("/see-other");
    REQUIRE(seeOther.ok());
    CHECK_EQ(seeOther->body, std::string("GET\n"));
    CHECK_EQ(seeOther->finalUrl, f.server.Url("/echo"));
    auto echoed = f.server.RequestsTo("/echo");
    REQUIRE_EQ(echoed.size(), std::size_t{1});
    CHECK_EQ(echoed[0].method, std::string("GET"));
    CHECK(echoed[0].FindHeader("Content-Type") == nullptr);

    auto temporary = post("/temporary");
    REQUIRE(temporary.ok());
    CHECK_EQ(temporary->body, std::string("POST\nform=1"));

    auto permanent = post("/permanent");
    REQUIRE(permanent.ok());
    CHECK_EQ(permanent->body, std::string("POST\nform=1"));
}

// ---- Timeouts and cancellation ---------------------------------------------------------------

AETHER_TEST("loopback: a server that never answers hits the receive timeout") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/stall-headers");
    request.receiveTimeout = 600ms;
    const auto started = Clock::now();
    auto response = f.client->Send(request, {});
    const auto elapsed = Clock::now() - started;
    TEST_NOTE("receive timeout after " + std::to_string(Ms(elapsed)) + " ms: " + Text(response));
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Timeout);
    CHECK_EQ(f.server.CountRequests("/stall-headers"), std::size_t{1});  // no silent resend
    // WinHTTP checks timeouts on a coarse internal timer: measured on Windows 11 (build 26300),
    // requests of 200-3000 ms all fired after about 4 s and 5000 ms after about 8 s. Hence the
    // generous upper bound; see docs/modules/net.md.
    CHECK(elapsed >= 500ms);
    CHECK(elapsed < 10s);
}

AETHER_TEST("loopback: a body that stalls hits the receive timeout") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/stall-body");
    request.receiveTimeout = 600ms;
    const auto started = Clock::now();
    auto response = f.client->Send(request, {});
    const auto elapsed = Clock::now() - started;
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Timeout);
    CHECK(elapsed < 10s);
}

AETHER_TEST("loopback: cancellation while waiting for headers aborts promptly") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/stall-headers");
    request.receiveTimeout = 30s;
    CancellationSource source;
    std::optional<Result<HttpResponse>> result;
    Clock::time_point finished;
    std::thread worker([&] {
        auto response = f.client->Send(request, source.Token());
        finished = Clock::now();
        result.emplace(std::move(response));
    });
    // Wait until the request is on the wire and WinHttpReceiveResponse is blocked.
    const bool arrived = f.server.WaitForRequests("/stall-headers", 1, 10s);
    std::this_thread::sleep_for(200ms);
    const auto cancelledAt = Clock::now();
    source.Cancel();
    const auto cancelReturned = Clock::now();
    worker.join();

    CHECK(arrived);
    CHECK(cancelReturned - cancelledAt < kMaxCancelCallDuration);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->ok());
    CHECK_EQ(result->error().code, ErrorCode::Cancelled);
    TEST_NOTE("returned " + std::to_string(Ms(finished - cancelledAt)) + " ms after Cancel()");
    CHECK(finished - cancelledAt < 3s);
    CHECK(f.server.WaitForDisconnects(1, 5s));
}

AETHER_TEST("loopback: cancellation mid-transfer aborts the blocking read") {
    Fixture f;
    REQUIRE(f.ready);
    auto request = f.Request("/stall-body");
    request.receiveTimeout = 30s;
    CancellationSource source;

    std::mutex mutex;
    std::condition_variable changed;
    std::size_t delivered = 0;
    std::optional<Result<HttpResponse>> result;
    Clock::time_point finished;
    std::thread worker([&] {
        auto response = f.client->SendStreaming(
            request,
            [&](std::span<const std::byte> chunk) -> Result<void> {
                std::lock_guard lock(mutex);
                delivered += chunk.size();
                changed.notify_all();
                return Ok();
            },
            {}, source.Token());
        std::lock_guard lock(mutex);
        finished = Clock::now();
        result.emplace(std::move(response));
        changed.notify_all();
    });
    bool gotData = false;
    {
        std::unique_lock lock(mutex);
        gotData = changed.wait_for(lock, 10s, [&] { return delivered >= kStallPrefix || result.has_value(); });
    }
    // The server has sent everything it will send; the worker is now blocked in WinHttpReadData.
    std::this_thread::sleep_for(200ms);
    const auto cancelledAt = Clock::now();
    source.Cancel();
    const auto cancelReturned = Clock::now();
    worker.join();

    CHECK(gotData);
    CHECK(cancelReturned - cancelledAt < kMaxCancelCallDuration);
    CHECK_EQ(delivered, kStallPrefix);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->ok());
    CHECK_EQ(result->error().code, ErrorCode::Cancelled);
    TEST_NOTE("returned " + std::to_string(Ms(finished - cancelledAt)) + " ms after Cancel()");
    CHECK(finished - cancelledAt < 3s);
    // Closing the request handle really closed the connection.
    CHECK(f.server.WaitForDisconnects(1, 5s));
}

AETHER_TEST("loopback: cancelling from the sink stops the transfer between reads") {
    Fixture f;
    REQUIRE(f.ready);
    CancellationSource source;
    int chunks = 0;
    auto response = f.client->SendStreaming(
        f.Request("/stream"),
        [&](std::span<const std::byte>) -> Result<void> {
            ++chunks;
            source.Cancel();  // runs the cancellation callback on this very thread
            return Ok();
        },
        {}, source.Token());
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Cancelled);
    CHECK_EQ(chunks, 1);
}

AETHER_TEST("loopback: a failing sink aborts the transfer and its error is returned") {
    Fixture f;
    REQUIRE(f.ready);
    int chunks = 0;
    auto response = f.client->SendStreaming(
        f.Request("/stream"),
        [&](std::span<const std::byte>) -> Result<void> {
            ++chunks;
            return MakeError(ErrorCode::InsufficientSpace, "test sink is full");
        },
        {}, {});
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::InsufficientSpace);
    CHECK_EQ(response.error().message, std::string("test sink is full"));
    CHECK_EQ(chunks, 1);

    auto throwing = f.client->SendStreaming(
        f.Request("/stream"), [](std::span<const std::byte>) -> Result<void> { throw std::runtime_error("boom"); },
        {}, {});
    REQUIRE_FALSE(throwing.ok());
    CHECK_EQ(throwing.error().code, ErrorCode::Internal);
}

// ---- Streaming -------------------------------------------------------------------------------

AETHER_TEST("loopback: streaming delivers 64 KB chunks and progress with the declared total") {
    Fixture f;
    REQUIRE(f.ready);
    std::vector<std::size_t> sizes;
    std::string received;
    std::vector<std::pair<std::uint64_t, std::optional<std::uint64_t>>> progress;
    auto response = f.client->SendStreaming(
        f.Request("/stream"),
        [&](std::span<const std::byte> chunk) -> Result<void> {
            sizes.push_back(chunk.size());
            received.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            return Ok();
        },
        [&](std::uint64_t bytes, std::optional<std::uint64_t> total) { progress.emplace_back(bytes, total); }, {});
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    CHECK(response->body.empty());
    CHECK_EQ(HeaderOf(*response, "Content-Type"), std::string("application/octet-stream"));

    const std::vector<std::size_t> expected{65'536, 65'536, 65'536, kStreamSize - 3 * 65'536};
    CHECK(sizes == expected);
    CHECK(received == Pattern(kStreamSize));

    REQUIRE_FALSE(progress.empty());
    std::uint64_t previous = 0;
    bool monotonic = true;
    bool totalsKnown = true;
    for (const auto& [bytes, total] : progress) {
        monotonic = monotonic && bytes > previous;
        totalsKnown = totalsKnown && total == std::optional<std::uint64_t>(kStreamSize);
        previous = bytes;
    }
    CHECK(monotonic);
    CHECK(totalsKnown);
    CHECK_EQ(progress.back().first, std::uint64_t{kStreamSize});

    // The buffered variant returns the same bytes.
    auto buffered = f.client->Send(f.Request("/stream"), {});
    REQUIRE(buffered.ok());
    CHECK(buffered->body == Pattern(kStreamSize));
}

AETHER_TEST("loopback: streaming a chunked body reports an unknown total") {
    Fixture f;
    REQUIRE(f.ready);
    std::vector<std::size_t> sizes;
    std::string received;
    std::uint64_t lastProgress = 0;
    bool anyTotal = false;
    auto response = f.client->SendStreaming(
        f.Request("/stream-chunked"),
        [&](std::span<const std::byte> chunk) -> Result<void> {
            sizes.push_back(chunk.size());
            received.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
            return Ok();
        },
        [&](std::uint64_t bytes, std::optional<std::uint64_t> total) {
            lastProgress = bytes;
            anyTotal = anyTotal || total.has_value();
        },
        {});
    REQUIRE(response.ok());
    const std::vector<std::size_t> expected{65'536, 65'536, kChunkedStreamSize - 2 * 65'536};
    CHECK(sizes == expected);
    CHECK(received == Pattern(kChunkedStreamSize));
    CHECK_EQ(lastProgress, std::uint64_t{kChunkedStreamSize});
    CHECK_FALSE(anyTotal);
}

AETHER_TEST("loopback: gzip bodies are decoded and the size limit applies to decoded bytes") {
    Fixture f;
    REQUIRE(f.ready);
    if (!f.client->SessionInfo().decompressionEnabled) {
        TEST_NOTE("WinHTTP decompression is unavailable on this machine; skipped");
        return;
    }
    auto hello = f.client->Send(f.Request("/gzip"), {});
    REQUIRE(hello.ok());
    CHECK_EQ(hello->body, std::string("decoded by WinHTTP"));
    // Observed WinHTTP behaviour (documented in docs/modules/net.md), recorded rather than asserted.
    TEST_NOTE(std::string("decoded response keeps Content-Encoding: ") +
              (hello->FindHeader("Content-Encoding") ? "yes" : "no") +
              ", Content-Length: " + (hello->FindHeader("Content-Length") ? "yes" : "no"));
    const auto received = f.server.RequestsTo("/gzip");
    REQUIRE_EQ(received.size(), std::size_t{1});
    const std::string* acceptEncoding = received[0].FindHeader("Accept-Encoding");
    REQUIRE(acceptEncoding != nullptr);
    CHECK(Contains(*acceptEncoding, "gzip"));

    // 290 bytes on the wire that expand to 256 KiB: rejected although Content-Length is tiny.
    auto bombRequest = f.Request("/gzip-bomb");
    bombRequest.maxResponseBytes = 64 * 1024;
    auto bomb = f.client->Send(bombRequest, {});
    REQUIRE_FALSE(bomb.ok());
    CHECK_EQ(bomb.error().code, ErrorCode::Integrity);

    bombRequest.maxResponseBytes = 1024 * 1024;
    std::optional<std::uint64_t> reportedTotal = 1;
    std::uint64_t receivedBytes = 0;
    auto decoded = f.client->SendStreaming(
        bombRequest, [](std::span<const std::byte>) -> Result<void> { return Ok(); },
        [&](std::uint64_t bytes, std::optional<std::uint64_t> total) {
            receivedBytes = bytes;
            reportedTotal = total;
        },
        {});
    REQUIRE(decoded.ok());
    CHECK_EQ(receivedBytes, std::uint64_t{kGzipBombDecodedSize});
    // The 290-byte Content-Length is never reported as the total of the 256 KiB decoded stream.
    CHECK_FALSE(reportedTotal.has_value());

    WinHttpClientOptions options;
    options.enableDecompression = false;
    auto raw = WinHttpClient::Create(options);
    REQUIRE(raw.ok());
    auto encoded = raw.value()->Send(f.Request("/gzip"), {});
    REQUIRE(encoded.ok());
    CHECK(encoded->body == GzipHello());
}

// ---- Concurrency and transport errors --------------------------------------------------------

AETHER_TEST("loopback: one client serves concurrent requests from several threads") {
    Fixture f;
    REQUIRE(f.ready);
    constexpr int kThreads = 8;
    constexpr int kRequestsPerThread = 6;
    std::atomic<int> succeeded{0};
    std::mutex failuresMutex;
    std::vector<std::string> failures;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kRequestsPerThread; ++i) {
                const bool stream = (t + i) % 2 == 0;
                auto response = f.client->Send(f.Request(stream ? "/stream" : "/ok", t % 2 ? "localhost" : "127.0.0.1"), {});
                const bool good = response.ok() && response->status == 200 &&
                                  (stream ? response->body == Pattern(kStreamSize)
                                          : response->body == "hello from loopback");
                if (good) {
                    ++succeeded;
                } else {
                    std::lock_guard lock(failuresMutex);
                    failures.push_back(Text(response));
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    for (const auto& failure : failures) TEST_NOTE("concurrent request failed: " + failure);
    CHECK_EQ(succeeded.load(), kThreads * kRequestsPerThread);
}

AETHER_TEST("loopback: concurrent requests cancelled at arbitrary moments end cleanly") {
    Fixture f;
    REQUIRE(f.ready);
    constexpr int kThreads = 6;
    constexpr int kRequestsPerThread = 10;
    const std::string expected = Pattern(kStreamSize);
    std::atomic<int> completed{0};
    std::atomic<int> cancelled{0};
    std::mutex failuresMutex;
    std::vector<std::string> failures;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kRequestsPerThread; ++i) {
                CancellationSource source;
                // Spread the cancel over 0-60 ms so it lands before the call, during connect, send,
                // headers and body, and after completion. A spin wait, because sleeps are rounded up to
                // the system timer resolution.
                const auto delay = std::chrono::microseconds((t * 7'919 + i * 6'007) % 60'000);
                std::thread canceller([&source, delay] {
                    const auto until = Clock::now() + delay;
                    while (Clock::now() < until) std::this_thread::yield();
                    source.Cancel();
                });
                auto response = f.client->Send(f.Request("/stream"), source.Token());
                canceller.join();
                if (response.ok() && response->status == 200 && response->body == expected) {
                    ++completed;
                } else if (!response.ok() && response.error().code == ErrorCode::Cancelled) {
                    ++cancelled;
                } else {
                    // Anything else (a short or foreign body, a transport error) means cancelling one
                    // request disturbed another or left a request half-finished.
                    std::lock_guard lock(failuresMutex);
                    failures.push_back(Text(response) + (response.ok() ? " with a wrong body" : ""));
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
    for (const auto& failure : failures) TEST_NOTE("unexpected outcome: " + failure);
    TEST_NOTE(std::to_string(completed.load()) + " completed, " + std::to_string(cancelled.load()) + " cancelled");
    CHECK(failures.empty());
    CHECK_EQ(completed.load() + cancelled.load(), kThreads * kRequestsPerThread);
    CHECK(cancelled.load() > 0);

    // The client is still fully usable afterwards.
    auto after = f.client->Send(f.Request("/ok"), {});
    REQUIRE(after.ok());
    CHECK_EQ(after->status, 200);
}

AETHER_TEST("loopback: connection refused maps to Network") {
    Fixture f;
    REQUIRE(f.ready);
    // Reserve a port, then release it so nothing listens there.
    std::uint16_t closedPort = 0;
    {
        LoopbackHttpServer probe;
        REQUIRE(probe.Start());
        closedPort = probe.Port();
    }
    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(closedPort) + "/";
    request.allowInsecureLoopback = true;
    request.connectTimeout = 15s;
    const auto started = Clock::now();
    auto response = f.client->Send(request, {});
    TEST_NOTE("refused after " + std::to_string(Ms(Clock::now() - started)) + " ms: " + Text(response));
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Network);
    CHECK(Contains(response.error().message, "http://127.0.0.1:" + std::to_string(closedPort)));
}

AETHER_TEST("loopback: https to a plain-text server fails with a Network error") {
    Fixture f;
    REQUIRE(f.ready);
    HttpRequest request;
    request.url = "https://127.0.0.1:" + std::to_string(f.server.Port()) + "/ok";
    request.connectTimeout = 5s;
    request.receiveTimeout = 5s;
    auto response = f.client->Send(request, {});
    TEST_NOTE("https to plain text: " + Text(response));
    REQUIRE_FALSE(response.ok());
    CHECK_EQ(response.error().code, ErrorCode::Network);
}
