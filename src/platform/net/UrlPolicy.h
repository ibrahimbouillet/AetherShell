// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure URL and redirect policy used by WinHttpClient. Nothing here performs I/O,
// so every rule is unit tested without a network:
//
//  * request URLs must be absolute https:// URLs (plain http:// only for loopback hosts when the
//    request explicitly opts in with `allowInsecureLoopback`, which only tests do);
//  * user information ("user:password@") and empty hosts are rejected;
//  * redirects are followed only to hosts the caller allow-listed, never from https to http.
//
// URLs are UTF-8 std::string values but must be pure ASCII: callers percent-encode anything else
// and convert international host names to Punycode (WinHTTP does not do it for them).
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace aether::net {

struct ParsedUrl {
    bool secure = true;        // https
    std::string host;          // as written in the URL; IPv6 literals without brackets ("::1")
    std::uint16_t port = 0;    // explicit or scheme default (443 / 80)
    std::string pathAndQuery;  // origin-form request target ("/" when the URL has no path); no fragment

    // "https://host[:port]" (no path or query, which may carry secrets); the port is omitted when it is
    // the scheme default. Used in logs and error messages.
    [[nodiscard]] std::string Origin() const;
    // Host as WinHttpConnect expects it (IPv6 literals in brackets).
    [[nodiscard]] std::string ConnectHost() const;
};

// Upper bound on accepted URL length, in characters.
inline constexpr std::size_t kMaxUrlLength = 8 * 1024;

// True for exactly "localhost", "127.0.0.1", "::1" and "[::1]" (case-insensitive).
[[nodiscard]] bool IsLoopbackHost(std::string_view host) noexcept;

// Validates and splits a request URL with WinHttpCrackUrl. Fails with ErrorCode::InvalidArgument and
// a message that never echoes the URL (query strings may contain API keys).
[[nodiscard]] Result<ParsedUrl> ParseRequestUrl(std::string_view url, bool allowInsecureLoopback);

// 301, 302, 303, 307 and 308: the statuses WinHttpClient follows itself.
[[nodiscard]] bool IsRedirectStatus(int status) noexcept;

// Exact, ASCII case-insensitive host match against the allow-list. Brackets around IPv6 literals are
// ignored on both sides. An empty allow-list allows nothing.
[[nodiscard]] bool IsRedirectHostAllowed(std::string_view host, std::span<const std::string> allowedHosts) noexcept;

// Resolves a Location header value against the absolute URL that produced it (RFC 3986 section 5.2,
// including dot-segment removal). The result has no fragment.
[[nodiscard]] Result<std::string> ResolveReference(std::string_view baseUrl, std::string_view reference);

// Applies the redirect policy to an already resolved absolute target URL:
//   1. the target must itself be a valid request URL (https, or loopback http when allowed);
//   2. a redirect must never downgrade https to http;
//   3. the target host must be in `allowedHosts`.
// Policy violations fail with ErrorCode::Validation ("redirect to untrusted host ...").
[[nodiscard]] Result<ParsedUrl> EvaluateRedirect(const ParsedUrl& from, std::string_view targetUrl,
                                                 std::span<const std::string> allowedHosts,
                                                 bool allowInsecureLoopback);

}  // namespace aether::net
