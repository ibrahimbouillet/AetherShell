// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Fakes and builders shared by the update tests: an in-memory IHttpClient, a
// manually fired IUpdateTimer, GitHub release JSON builders and the
// deterministic installer payloads that match UpdateTestFixtures.h.
#pragma once

#include "UpdateTestFixtures.h"

#include "core/Http.h"
#include "core/Json.h"
#include "core/Strings.h"
#include "updates/SignatureVerifier.h"
#include "updates/TrustedKeys.h"
#include "updates/UpdateTimer.h"
#include "updates/UpdateTypes.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace aether::updates::testing {

inline const GitHubRepository kTestRepository{"ibrahimbouillet", "AetherShell"};
inline constexpr std::string_view kWebPrefix = "https://github.com/ibrahimbouillet/AetherShell/";
inline constexpr std::string_view kLatestUrl = "https://api.github.com/repos/ibrahimbouillet/AetherShell/releases/latest";
inline constexpr std::string_view kListUrl =
    "https://api.github.com/repos/ibrahimbouillet/AetherShell/releases?per_page=30";

// Same formula as tests/unit/updates/generate_test_fixtures.py.
inline std::string TestAssetBytes(std::uint64_t size, std::uint32_t seed) {
    std::string bytes(static_cast<std::size_t>(size), '\0');
    for (std::uint64_t i = 0; i < size; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<char>((i * 131 + seed * 17 + (i >> 8)) & 0xFF);
    }
    return bytes;
}

inline EcdsaP256Verifier TestVerifier() {
    const std::array keys{trust::TrustedKey{"test-key", fixtures::kTestPublicKeyHex}};
    return EcdsaP256Verifier(keys);
}

inline EcdsaP256Verifier UnrelatedVerifier() {
    const std::array keys{trust::TrustedKey{"unrelated-key", fixtures::kUnrelatedPublicKeyHex}};
    return EcdsaP256Verifier(keys);
}

// ---------------------------------------------------------------------------------------------
// In-memory HTTP client.

struct FakeResponse {
    int status = 200;
    std::vector<net::HttpHeader> headers;
    std::string body;
    std::optional<Error> error;            // transport failure instead of a response
    std::optional<std::size_t> failAfter;  // streaming: deliver this many body bytes, then fail with `error`
    std::optional<std::size_t> hangAfter;  // streaming: deliver this many bytes, then block until cancelled
    std::string finalUrl;                  // empty: the request URL (no redirect)
    std::size_t chunkSize = 16 * 1024;
    bool announceLength = true;            // report the body length as the total through HttpProgress
    bool enforceLimit = true;              // fail with Integrity beyond maxResponseBytes, like the real client
};

class FakeHttpClient final : public net::IHttpClient {
public:
    // Responses for `url` are served in order; the last one repeats. Unknown URLs get HTTP 404.
    void Add(std::string url, FakeResponse response) {
        std::lock_guard lock(mutex_);
        routes_[std::move(url)].push_back(std::move(response));
    }
    void Set(std::string url, FakeResponse response) {
        std::lock_guard lock(mutex_);
        auto& queue = routes_[std::move(url)];
        queue.clear();
        queue.push_back(std::move(response));
    }
    void Clear() {
        std::lock_guard lock(mutex_);
        routes_.clear();
    }

    [[nodiscard]] std::vector<net::HttpRequest> Requests() const {
        std::lock_guard lock(mutex_);
        return requests_;
    }
    [[nodiscard]] std::size_t CountFor(std::string_view url) const {
        std::lock_guard lock(mutex_);
        std::size_t count = 0;
        for (const auto& request : requests_) count += request.url == url ? 1 : 0;
        return count;
    }
    [[nodiscard]] std::optional<net::HttpRequest> LastRequestFor(std::string_view url) const {
        std::lock_guard lock(mutex_);
        for (auto it = requests_.rbegin(); it != requests_.rend(); ++it) {
            if (it->url == url) return *it;
        }
        return std::nullopt;
    }

    // Invoked (on the calling thread) when a request arrives, before it is answered.
    std::function<void(const net::HttpRequest&)> onRequest;
    // Invoked after each streamed chunk with the bytes delivered so far.
    std::function<void(std::uint64_t delivered)> onChunk;

    Result<net::HttpResponse> Send(const net::HttpRequest& request, const CancellationToken& cancel) override {
        auto route = Begin(request);
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "fake: cancelled");
        if (route.error) return *route.error;
        if (route.enforceLimit && route.body.size() > request.maxResponseBytes) {
            return MakeError(ErrorCode::Integrity, "fake: response exceeds maxResponseBytes");
        }
        net::HttpResponse response;
        response.status = route.status;
        response.headers = route.headers;
        response.body = route.body;
        response.finalUrl = route.finalUrl.empty() ? request.url : route.finalUrl;
        return response;
    }

    Result<net::HttpResponse> SendStreaming(const net::HttpRequest& request, const net::HttpChunkSink& sink,
                                            const net::HttpProgress& progress,
                                            const CancellationToken& cancel) override {
        auto route = Begin(request);
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "fake: cancelled");
        if (route.error && !route.failAfter) return *route.error;
        const std::optional<std::uint64_t> total =
            route.announceLength ? std::optional<std::uint64_t>(route.body.size()) : std::nullopt;
        if (progress) progress(0, total);
        const std::size_t limit = route.failAfter ? std::min(*route.failAfter, route.body.size()) : route.body.size();
        std::size_t delivered = 0;
        while (delivered < limit) {
            const auto size = std::min(route.chunkSize, limit - delivered);
            if (route.enforceLimit && delivered + size > request.maxResponseBytes) {
                return MakeError(ErrorCode::Integrity, "fake: response exceeds maxResponseBytes");
            }
            const auto chunk = std::as_bytes(std::span(route.body.data() + delivered, size));
            if (auto accepted = sink(chunk); !accepted) return accepted.error();
            delivered += size;
            if (progress) progress(delivered, total);
            if (onChunk) onChunk(delivered);
            if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "fake: cancelled");
            if (route.hangAfter && delivered >= *route.hangAfter) {
                // Simulates a stalled connection: only cancellation ends it (bounded so a bug cannot
                // hang the test run).
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (!cancel.IsCancelled() && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return MakeError(ErrorCode::Cancelled, "fake: cancelled while stalled");
            }
        }
        if (route.failAfter) {
            return route.error ? *route.error : MakeError(ErrorCode::Network, "fake: connection reset");
        }
        net::HttpResponse response;
        response.status = route.status;
        response.headers = route.headers;
        response.finalUrl = route.finalUrl.empty() ? request.url : route.finalUrl;
        return response;
    }

private:
    FakeResponse Begin(const net::HttpRequest& request) {
        FakeResponse route;
        {
            std::lock_guard lock(mutex_);
            requests_.push_back(request);
            auto it = routes_.find(request.url);
            if (it == routes_.end() || it->second.empty()) {
                route.status = 404;
                route.body = R"({"message":"Not Found"})";
            } else {
                route = it->second.front();
                if (it->second.size() > 1) it->second.pop_front();
            }
        }
        if (onRequest) onRequest(request);
        return route;
    }

    mutable std::mutex mutex_;
    std::map<std::string, std::deque<FakeResponse>, std::less<>> routes_;
    std::vector<net::HttpRequest> requests_;
};

inline bool HasHeader(const net::HttpRequest& request, std::string_view name, std::string_view value) {
    for (const auto& header : request.headers) {
        if (text::EqualsIgnoreCaseAscii(header.name, name) && header.value == value) return true;
    }
    return false;
}

inline bool HasHeaderNamed(const net::HttpRequest& request, std::string_view name) {
    for (const auto& header : request.headers) {
        if (text::EqualsIgnoreCaseAscii(header.name, name)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Manually fired timer.

class FakeUpdateTimer final : public IUpdateTimer {
public:
    void Arm(std::chrono::milliseconds delay, std::function<void()> callback) override {
        std::lock_guard lock(mutex_);
        armed_ = true;
        delay_ = delay;
        callback_ = std::move(callback);
        ++armCount_;
    }
    void Disarm() noexcept override {
        std::lock_guard lock(mutex_);
        armed_ = false;
        callback_ = nullptr;
    }

    [[nodiscard]] bool IsArmed() const {
        std::lock_guard lock(mutex_);
        return armed_;
    }
    [[nodiscard]] std::chrono::milliseconds Delay() const {
        std::lock_guard lock(mutex_);
        return delay_;
    }
    [[nodiscard]] int ArmCount() const {
        std::lock_guard lock(mutex_);
        return armCount_;
    }

    // Runs the pending callback as if the deadline had passed. False when nothing is armed.
    bool Fire() {
        std::function<void()> callback;
        {
            std::lock_guard lock(mutex_);
            if (!armed_) return false;
            armed_ = false;
            callback = std::move(callback_);
        }
        if (callback) callback();
        return static_cast<bool>(callback);
    }

private:
    mutable std::mutex mutex_;
    bool armed_ = false;
    std::chrono::milliseconds delay_{0};
    std::function<void()> callback_;
    int armCount_ = 0;
};

// ---------------------------------------------------------------------------------------------
// GitHub release JSON.

struct TestAsset {
    std::string name;
    std::uint64_t size = 0;
    std::string url;  // empty: the official download URL for the release tag
};

struct TestRelease {
    std::string tag;
    bool prerelease = false;
    bool draft = false;
    std::string name;
    std::string body;
    std::string htmlUrl;  // empty: <web prefix>releases/tag/<tag>
    std::string publishedAt = "2026-10-01T12:00:00Z";
    std::vector<TestAsset> assets;
};

inline std::string DownloadUrl(std::string_view tag, std::string_view fileName) {
    return std::string(kWebPrefix) + "releases/download/" + std::string(tag) + "/" + std::string(fileName);
}

inline json::Value ReleaseJson(const TestRelease& release) {
    auto assets = json::Value::MakeArray();
    std::uint64_t id = 1000;
    for (const auto& asset : release.assets) {
        auto entry = json::Value::MakeObject();
        entry.Set("url", "https://api.github.com/repos/ibrahimbouillet/AetherShell/releases/assets/" +
                             std::to_string(id));
        entry.Set("id", json::Value(id++));
        entry.Set("name", asset.name);
        entry.Set("label", json::Value());
        entry.Set("content_type", "application/octet-stream");
        entry.Set("state", "uploaded");
        entry.Set("size", json::Value(asset.size));
        entry.Set("download_count", 3);
        entry.Set("browser_download_url", asset.url.empty() ? DownloadUrl(release.tag, asset.name) : asset.url);
        assets.Append(std::move(entry));
    }
    auto object = json::Value::MakeObject();
    object.Set("url", "https://api.github.com/repos/ibrahimbouillet/AetherShell/releases/1");
    object.Set("html_url",
               release.htmlUrl.empty() ? std::string(kWebPrefix) + "releases/tag/" + release.tag : release.htmlUrl);
    object.Set("id", 1);
    object.Set("tag_name", release.tag);
    object.Set("target_commitish", "main");
    object.Set("name", release.name.empty() ? release.tag : release.name);
    object.Set("draft", release.draft);
    object.Set("prerelease", release.prerelease);
    object.Set("created_at", "2026-10-01T11:00:00Z");
    object.Set("published_at", release.draft ? json::Value() : json::Value(release.publishedAt));
    object.Set("assets", std::move(assets));
    object.Set("body", release.body);
    return object;
}

inline std::string ReleaseBody(const TestRelease& release) { return json::Serialize(ReleaseJson(release)); }

inline std::string ReleaseListBody(const std::vector<TestRelease>& releases) {
    auto list = json::Value::MakeArray();
    for (const auto& release : releases) list.Append(ReleaseJson(release));
    return json::Serialize(list);
}

inline std::string ManifestName(std::string_view version) {
    return "AetherShell-" + std::string(version) + "-manifest.json";
}
inline std::string SignatureName(std::string_view version) { return ManifestName(version) + ".sig"; }
inline std::string ChecksumsName(std::string_view version) {
    return "AetherShell-" + std::string(version) + "-checksums.txt";
}

// The GitHub release that publishes `fixture` under `tag` (default "v<version>").
inline TestRelease FixtureRelease(const fixtures::SignedRelease& fixture, std::string tag = {}) {
    TestRelease release;
    release.tag = tag.empty() ? "v" + std::string(fixture.version) : std::move(tag);
    release.prerelease = fixture.channel == "prerelease";
    release.name = "Aether Shell " + std::string(fixture.version);
    release.body = "## Changes\r\n\r\n\r\n\r\n* Faster dock\x07\r\n";
    for (const auto& asset : fixture.assets) release.assets.push_back({std::string(asset.fileName), asset.size, {}});
    release.assets.push_back({ManifestName(fixture.version), fixture.manifest.size(), {}});
    release.assets.push_back({SignatureName(fixture.version), fixture.signature.size(), {}});
    release.assets.push_back({ChecksumsName(fixture.version), fixture.checksums.size(), {}});
    return release;
}

inline FakeResponse Ok200(std::string body, std::vector<net::HttpHeader> headers = {}) {
    FakeResponse response;
    response.status = 200;
    response.body = std::move(body);
    response.headers = std::move(headers);
    return response;
}

inline FakeResponse Status(int status, std::vector<net::HttpHeader> headers = {}) {
    FakeResponse response;
    response.status = status;
    response.headers = std::move(headers);
    response.body = R"({"message":"error"})";
    return response;
}

inline FakeResponse TransportError(ErrorCode code, std::string message) {
    FakeResponse response;
    response.error = MakeError(code, std::move(message));
    return response;
}

// Serves the manifest, the signature and every installer of `fixture` at the download URLs of `tag`.
inline void ServeFixtureAssets(FakeHttpClient& http, const fixtures::SignedRelease& fixture, std::string_view tag) {
    http.Set(DownloadUrl(tag, ManifestName(fixture.version)), Ok200(std::string(fixture.manifest)));
    http.Set(DownloadUrl(tag, SignatureName(fixture.version)), Ok200(std::string(fixture.signature)));
    for (const auto& asset : fixture.assets) {
        http.Set(DownloadUrl(tag, asset.fileName), Ok200(TestAssetBytes(asset.size, asset.seed)));
    }
}

}  // namespace aether::updates::testing
