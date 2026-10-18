// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// WinHttpClient checks that need a WinHTTP session but no network I/O: the user agent, the session
// capabilities and argument validation (requests rejected before anything is sent).
#include "AetherTest.h"

#include "aether/BuildInfo.h"
#include "platform/net/WinHttpClient.h"

#include <atomic>
#include <string>

using namespace aether;
using namespace aether::net;

namespace {

std::unique_ptr<WinHttpClient> MakeClient() {
    auto client = WinHttpClient::Create();
    if (!client.ok()) {
        TEST_NOTE("WinHttpClient::Create failed: " + client.error().Describe());
        return nullptr;
    }
    return std::move(client).value();
}

}  // namespace

AETHER_TEST("winhttp client: user agent names the product, version and project URL") {
    const std::string agent = DefaultUserAgent();
    CHECK_EQ(agent, "AetherShell/" + std::string(build::kVersion) + " (+" + std::string(build::kProjectUrl) + ")");
    CHECK(agent.starts_with("AetherShell/"));
    CHECK(agent.find("(+https://github.com/") != std::string::npos);
}

AETHER_TEST("winhttp client: the session opens with TLS 1.2+ and reports its capabilities") {
    auto client = MakeClient();
    REQUIRE(client != nullptr);
    const auto& info = client->SessionInfo();
    CHECK_EQ(info.userAgent, DefaultUserAgent());
    // HTTP/2 and decompression exist on every supported Windows build (10 2004 and later).
    CHECK(info.http2Enabled);
    CHECK(info.decompressionEnabled);
    TEST_NOTE(std::string("TLS 1.3 offered: ") + (info.tls13Enabled ? "yes" : "no (TLS 1.2 only)"));

    WinHttpClientOptions minimal;
    minimal.enableHttp2 = false;
    minimal.enableDecompression = false;
    auto plain = WinHttpClient::Create(minimal);
    REQUIRE(plain.ok());
    CHECK_FALSE(plain.value()->SessionInfo().http2Enabled);
    CHECK_FALSE(plain.value()->SessionInfo().decompressionEnabled);
}

AETHER_TEST("winhttp client: invalid requests are rejected before any I/O") {
    auto client = MakeClient();
    REQUIRE(client != nullptr);
    const CancellationToken never;

    const auto expectInvalid = [&](const HttpRequest& request) {
        auto result = client->Send(request, never);
        CHECK_FALSE(result.ok());
        if (!result.ok()) CHECK_EQ(result.error().code, ErrorCode::InvalidArgument);
    };

    HttpRequest insecure;
    insecure.url = "http://example.com/";
    expectInvalid(insecure);

    HttpRequest insecureEvenWithFlag;
    insecureEvenWithFlag.url = "http://example.com/";
    insecureEvenWithFlag.allowInsecureLoopback = true;
    expectInvalid(insecureEvenWithFlag);

    HttpRequest userInfo;
    userInfo.url = "https://user:pw@example.com/";
    expectInvalid(userInfo);

    HttpRequest badMethod;
    badMethod.url = "https://example.com/";
    badMethod.method = "GE T";
    expectInvalid(badMethod);

    HttpRequest injected;
    injected.url = "https://example.com/";
    injected.headers.push_back({"Accept", "x\r\nX-Injected: 1"});
    expectInvalid(injected);

    HttpRequest hostOverride;
    hostOverride.url = "https://example.com/";
    hostOverride.headers.push_back({"Host", "other.example"});
    expectInvalid(hostOverride);

    HttpRequest zeroTimeout;
    zeroTimeout.url = "https://example.com/";
    zeroTimeout.receiveTimeout = std::chrono::milliseconds{0};
    expectInvalid(zeroTimeout);

    HttpRequest negativeTimeout;
    negativeTimeout.url = "https://example.com/";
    negativeTimeout.connectTimeout = std::chrono::milliseconds{-5};
    expectInvalid(negativeTimeout);

    HttpRequest valid;
    valid.url = "https://example.com/";
    auto noSink = client->SendStreaming(valid, HttpChunkSink{}, HttpProgress{}, never);
    REQUIRE_FALSE(noSink.ok());
    CHECK_EQ(noSink.error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("winhttp client: an already cancelled token returns Cancelled without I/O") {
    auto client = MakeClient();
    REQUIRE(client != nullptr);
    CancellationSource source;
    source.Cancel();

    HttpRequest request;
    request.url = "https://example.com/";
    auto result = client->Send(request, source.Token());
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, ErrorCode::Cancelled);

    std::atomic<int> chunks{0};
    auto streamed = client->SendStreaming(
        request,
        [&](std::span<const std::byte>) -> Result<void> {
            ++chunks;
            return Ok();
        },
        {}, source.Token());
    REQUIRE_FALSE(streamed.ok());
    CHECK_EQ(streamed.error().code, ErrorCode::Cancelled);
    CHECK_EQ(chunks.load(), 0);
}
