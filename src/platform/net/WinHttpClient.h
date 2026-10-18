// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Production IHttpClient built on WinHTTP in synchronous mode. Every call blocks the calling worker
// thread (never call it from the UI thread) and can be aborted through its CancellationToken.
//
//   auto client = aether::net::WinHttpClient::Create();
//   if (!client) return client.error();
//   aether::net::HttpRequest request;
//   request.url = "https://api.github.com/zen";
//   auto response = client.value()->Send(request, cancel);
//
// Security properties (see docs/modules/net.md):
//   * https only; TLS 1.2 and 1.3 only; certificate errors are never ignored;
//   * plain http only for loopback hosts when HttpRequest::allowInsecureLoopback is set (tests);
//   * redirects are followed manually and only to allow-listed hosts, never from https to http;
//     credential headers (Authorization, Proxy-Authorization, Cookie) are dropped when a redirect
//     changes the origin;
//   * response size is bounded by HttpRequest::maxResponseBytes (declared and actual length);
//   * no cookies are stored and nothing is cached.
//
// Thread safety: one instance may be used concurrently from any number of worker threads. The WinHTTP
// session is shared; each call uses its own connection and request handles. The client must outlive
// every call in flight (destroy it only after the workers using it have stopped).
#pragma once

#include "core/Http.h"
#include "core/Result.h"

#include <cstddef>
#include <memory>
#include <string>

namespace aether::net {

struct WinHttpClientOptions {
    // Offer HTTP/2 through ALPN when the OS supports it (Windows 10 1607 and later); HTTP/1.1 otherwise.
    bool enableHttp2 = true;
    // Advertise gzip/deflate and let WinHTTP decode transparently. Size limits apply to the decoded
    // bytes, so a compression bomb is still bounded by HttpRequest::maxResponseBytes.
    bool enableDecompression = true;
};

// What the session actually obtained from the OS; some options are unavailable on older builds.
struct WinHttpSessionInfo {
    std::string userAgent;
    // True when WinHTTP accepted the TLS 1.3 flag; whether TLS 1.3 is negotiated still depends on
    // Schannel and the server. False: the flag was rejected and the session is TLS 1.2 only.
    bool tls13Enabled = false;
    bool http2Enabled = false;
    bool decompressionEnabled = false;
};

// "AetherShell/<version> (+https://github.com/ibrahimbouillet/AetherShell)"
[[nodiscard]] std::string DefaultUserAgent();

class WinHttpClient final : public IHttpClient {
public:
    // Size of the chunks SendStreaming hands to the sink; only the last chunk may be shorter.
    static constexpr std::size_t kChunkSize = 64 * 1024;

    // Opens the shared WinHTTP session (no network I/O happens here). Fails with ErrorCode::Platform
    // when WinHTTP is unavailable or refuses the mandatory TLS settings.
    [[nodiscard]] static Result<std::unique_ptr<WinHttpClient>> Create(const WinHttpClientOptions& options = {});

    ~WinHttpClient() override;
    WinHttpClient(const WinHttpClient&) = delete;
    WinHttpClient& operator=(const WinHttpClient&) = delete;

    // See IHttpClient. Additional failure modes:
    //   InvalidArgument  bad URL, method, header or non-positive timeout (nothing is sent)
    //   Validation       redirect rejected by policy ("redirect to untrusted host ...")
    //   Http             too many redirects, or a redirect without a usable Location
    //   Integrity        body larger than maxResponseBytes (declared or streamed)
    //   Timeout / Network / Cancelled  transport failures
    Result<HttpResponse> Send(const HttpRequest& request, const CancellationToken& cancel) override;

    // Streams the body in kChunkSize chunks. `progress` is called after every read with the bytes
    // received so far and the total from Content-Length (nullopt when the response has none; WinHTTP
    // removes it from gzip/deflate bodies it decodes).
    Result<HttpResponse> SendStreaming(const HttpRequest& request, const HttpChunkSink& sink,
                                       const HttpProgress& progress, const CancellationToken& cancel) override;

    [[nodiscard]] const WinHttpSessionInfo& SessionInfo() const noexcept;

private:
    struct Session;
    explicit WinHttpClient(std::unique_ptr<Session> session) noexcept;
    std::unique_ptr<Session> session_;
};

}  // namespace aether::net
