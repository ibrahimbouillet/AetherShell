// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Transport-neutral HTTP contract. The production implementation lives in
// platform/net (WinHTTP); update and weather logic depend only on this
// interface so they can be tested with in-memory fakes.
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::net {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method = "GET";
    // Absolute https:// URL. Plain http:// is rejected unless `allowInsecureLoopback` is set and the
    // host is a loopback address (used only by tests).
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;  // request body for POST/PUT (rarely used)

    std::chrono::milliseconds connectTimeout{10'000};
    std::chrono::milliseconds receiveTimeout{20'000};

    // Responses larger than this fail with ErrorCode::Integrity (body) and abort the transfer.
    std::uint64_t maxResponseBytes = 4ull * 1024 * 1024;

    // Redirect policy. Redirects are followed only to https:// URLs whose host is in this list
    // (exact, case-insensitive match). An empty list disables redirects entirely.
    std::vector<std::string> allowedRedirectHosts;
    int maxRedirects = 5;

    bool allowInsecureLoopback = false;
};

struct HttpResponse {
    int status = 0;
    std::vector<HttpHeader> headers;
    std::string body;      // empty for streaming requests
    std::string finalUrl;  // URL after redirects

    // Case-insensitive header lookup; nullptr when absent.
    [[nodiscard]] const std::string* FindHeader(std::string_view name) const noexcept;
};

using HttpChunkSink = std::function<Result<void>(std::span<const std::byte> chunk)>;
using HttpProgress = std::function<void(std::uint64_t received, std::optional<std::uint64_t> total)>;

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // Blocking request; call from worker threads only, never from the UI thread.
    // Transport problems return Network/Timeout/Cancelled errors. Any HTTP status (including 4xx/5xx)
    // is returned as a successful Result so callers can interpret it.
    virtual Result<HttpResponse> Send(const HttpRequest& request, const CancellationToken& cancel) = 0;

    // Like Send, but streams the body to `sink` instead of buffering it. If the sink returns an error,
    // the transfer is aborted and that error is returned. `progress` may be empty.
    virtual Result<HttpResponse> SendStreaming(const HttpRequest& request, const HttpChunkSink& sink,
                                               const HttpProgress& progress, const CancellationToken& cancel) = 0;
};

}  // namespace aether::net
