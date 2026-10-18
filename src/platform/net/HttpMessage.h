// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure HTTP/1.1 message helpers used by WinHttpClient: request validation (so a caller can never
// inject extra header lines), response header parsing and body framing rules. No I/O.
#pragma once

#include "core/Http.h"
#include "core/Result.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::net {

// RFC 9110 token ("GET", "POST", "PROPFIND"...). Rejects empty strings, spaces and CR/LF.
[[nodiscard]] bool IsValidMethod(std::string_view method) noexcept;

// Every name must be a token and every value printable ASCII (HTAB allowed) without CR/LF.
// Host, Content-Length and Transfer-Encoding are rejected because WinHTTP derives them from the URL and
// the body; letting a caller override them would desynchronise framing or the redirect policy.
// Error messages name the offending header but never echo its value (it may be a credential).
[[nodiscard]] Result<void> ValidateRequestHeaders(std::span<const HttpHeader> headers);

// "Name: value\r\n" lines for WinHttpSendRequest. Call ValidateRequestHeaders first.
[[nodiscard]] std::string FormatRequestHeaders(std::span<const HttpHeader> headers);

// Parses WINHTTP_QUERY_RAW_HEADERS_CRLF output (converted to UTF-8). The status line is skipped,
// names and values are trimmed, obsolete line folding is joined with a space, and malformed lines
// without a colon are ignored. Repeated headers are kept in order.
[[nodiscard]] std::vector<HttpHeader> ParseRawHeaders(std::string_view raw);

// Case-insensitive lookup of the first header with this name; nullptr when absent.
[[nodiscard]] const std::string* FindHeader(std::span<const HttpHeader> headers, std::string_view name) noexcept;

// Declared body length. nullopt when the header is absent, not a plain decimal number, too large for
// 64 bits, or repeated with different values (the length is then unknown, not guessed).
[[nodiscard]] std::optional<std::uint64_t> ParseContentLength(std::span<const HttpHeader> headers);

// True when the response has exactly one Content-Encoding header whose value is "gzip" or "deflate"
// (ASCII case-insensitive): the only codings WinHTTP decodes when decompression is enabled. Measured on
// Windows 11 (build 26300): WinHTTP then removes Content-Encoding and Content-Length from the headers
// it returns; any other value ("br", "x-gzip", lists such as "gzip, br") is passed through unchanged,
// with a Content-Length that counts exactly the body bytes the caller reads. If a WinHTTP build decoded
// a body but kept its headers, this tells the caller that Content-Length counts encoded bytes.
[[nodiscard]] bool IsWinHttpDecodedCoding(std::span<const HttpHeader> headers) noexcept;

// False for responses that never carry a body: HEAD requests, 1xx, 204 and 304.
[[nodiscard]] bool ResponseHasBody(std::string_view method, int status) noexcept;

// Headers that carry credentials and therefore are dropped when a redirect changes the origin.
[[nodiscard]] bool IsCredentialHeader(std::string_view name) noexcept;

}  // namespace aether::net
