// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// WinHttpClient against real Internet hosts (label "network", excluded from default test runs).
// Proves that TLS works on this machine and that certificate problems are reported, never ignored.
// The badssl.com hosts deliberately serve broken certificates; they are a public test service.
#include "AetherTest.h"

#include "platform/net/WinHttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

using namespace aether;
using namespace aether::net;
using namespace std::chrono_literals;

namespace {

std::unique_ptr<WinHttpClient> MakeClient() {
    auto client = WinHttpClient::Create();
    if (!client.ok()) {
        TEST_NOTE("WinHttpClient::Create failed: " + client.error().Describe());
        return nullptr;
    }
    return std::move(client).value();
}

HttpRequest MakeRequest(std::string url) {
    HttpRequest request;
    request.url = std::move(url);
    request.connectTimeout = 15s;
    request.receiveTimeout = 20s;
    request.maxResponseBytes = 64 * 1024;
    return request;
}

bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

}  // namespace

AETHER_TEST("network: GET https://api.github.com/zen succeeds over TLS") {
    auto client = MakeClient();
    REQUIRE(client != nullptr);
    TEST_NOTE(std::string("session: TLS 1.3 ") + (client->SessionInfo().tls13Enabled ? "on" : "off") + ", HTTP/2 " +
              (client->SessionInfo().http2Enabled ? "on" : "off"));

    auto request = MakeRequest("https://api.github.com/zen");
    request.headers.push_back({"Accept", "application/vnd.github+json"});
    request.headers.push_back({"X-GitHub-Api-Version", "2022-11-28"});
    auto response = client->Send(request, {});
    if (!response.ok()) TEST_NOTE("request failed: " + response.error().Describe());
    REQUIRE(response.ok());

    const int status = response->status;
    TEST_NOTE("HTTP " + std::to_string(status) + ", " + std::to_string(response->body.size()) + " body bytes");
    // 403 / 429 mean the anonymous rate limit is exhausted: TLS and HTTP still worked end to end.
    CHECK(status == 200 || status == 403 || status == 429);
    CHECK_EQ(response->finalUrl, request.url);
    CHECK(response->FindHeader("Date") != nullptr);
    if (const auto* remaining = response->FindHeader("X-RateLimit-Remaining")) {
        TEST_NOTE("X-RateLimit-Remaining: " + *remaining);
    }
    if (status == 200) {
        CHECK_FALSE(response->body.empty());
        TEST_NOTE("zen: " + response->body);
    }
}

AETHER_TEST("network: certificate errors are reported as TLS failures, never ignored") {
    auto client = MakeClient();
    REQUIRE(client != nullptr);
    for (const std::string_view host :
         {"expired.badssl.com", "wrong.host.badssl.com", "self-signed.badssl.com", "untrusted-root.badssl.com"}) {
        auto response = client->Send(MakeRequest("https://" + std::string(host) + "/"), {});
        // Any HTTP answer from these hosts means a broken certificate was accepted.
        if (response.ok()) {
            TEST_NOTE(std::string(host) + ": unexpectedly succeeded with HTTP " + std::to_string(response->status));
            CHECK_FALSE(response.ok());
            continue;
        }
        const Error& error = response.error();
        TEST_NOTE(std::string(host) + ": " + error.Describe());
        // The public test service being unreachable says nothing about certificate handling.
        const auto code = static_cast<std::uint32_t>(error.platformCode);
        if (error.code == ErrorCode::Timeout || code == ERROR_WINHTTP_NAME_NOT_RESOLVED ||
            code == ERROR_WINHTTP_CANNOT_CONNECT) {
            TEST_NOTE(std::string(host) + " is unreachable from this machine; not evaluated");
            continue;
        }
        CHECK_EQ(error.code, ErrorCode::Network);
        CHECK(Contains(error.message, "TLS failure"));
    }
}

AETHER_TEST("network: an unresolvable host name maps to Network") {
    auto client = MakeClient();
    REQUIRE(client != nullptr);
    // ".invalid" is reserved (RFC 6761) and never resolves.
    auto response = client->Send(MakeRequest("https://aether-shell-test.invalid/"), {});
    if (response.ok()) {
        // A proxy may answer for hosts it cannot resolve; that is still not a fabricated success.
        TEST_NOTE("a proxy answered with HTTP " + std::to_string(response->status));
        CHECK(response->status >= 400);
        return;
    }
    TEST_NOTE(response.error().Describe());
    CHECK_EQ(response.error().code, ErrorCode::Network);
}
