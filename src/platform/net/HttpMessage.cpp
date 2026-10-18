// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/net/HttpMessage.h"

#include "core/Strings.h"

#include <charconv>

namespace aether::net {
namespace {

constexpr bool IsTokenChar(char c) noexcept {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    switch (c) {
        case '!': case '#': case '$': case '%': case '&': case '\'': case '*': case '+':
        case '-': case '.': case '^': case '_': case '`': case '|': case '~': return true;
        default: return false;
    }
}

bool IsToken(std::string_view text) noexcept {
    if (text.empty()) return false;
    for (const char c : text) {
        if (!IsTokenChar(c)) return false;
    }
    return true;
}

bool IsValidHeaderValue(std::string_view value) noexcept {
    for (const char c : value) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '\t') continue;
        if (u < 0x20 || u >= 0x7F) return false;
    }
    return true;
}

std::optional<std::uint64_t> ParseDecimal(std::string_view text) noexcept {
    text = text::Trim(text);
    if (text.empty()) return std::nullopt;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

}  // namespace

bool IsValidMethod(std::string_view method) noexcept { return method.size() <= 32 && IsToken(method); }

Result<void> ValidateRequestHeaders(std::span<const HttpHeader> headers) {
    for (const auto& header : headers) {
        if (!IsToken(header.name)) {
            return MakeError(ErrorCode::InvalidArgument,
                             "request header name '" + text::SanitizeForLog(header.name, 64) + "' is not valid");
        }
        if (!IsValidHeaderValue(header.value)) {
            return MakeError(ErrorCode::InvalidArgument,
                             "request header '" + header.name + "' has a value with control or non-ASCII characters");
        }
        if (text::EqualsIgnoreCaseAscii(header.name, "Host") ||
            text::EqualsIgnoreCaseAscii(header.name, "Content-Length") ||
            text::EqualsIgnoreCaseAscii(header.name, "Transfer-Encoding")) {
            return MakeError(ErrorCode::InvalidArgument,
                             "request header '" + header.name + "' is managed by the HTTP client and cannot be set");
        }
    }
    return Ok();
}

std::string FormatRequestHeaders(std::span<const HttpHeader> headers) {
    std::string block;
    for (const auto& header : headers) {
        block += header.name;
        block += ": ";
        block += text::Trim(std::string_view(header.value));
        block += "\r\n";
    }
    return block;
}

std::vector<HttpHeader> ParseRawHeaders(std::string_view raw) {
    std::vector<HttpHeader> headers;
    bool firstLine = true;
    while (!raw.empty()) {
        const auto newline = raw.find('\n');
        std::string_view line = raw.substr(0, newline);
        raw = newline == std::string_view::npos ? std::string_view{} : raw.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (firstLine) {
            firstLine = false;
            if (line.starts_with("HTTP/")) continue;  // status line
        }
        if (line.empty()) continue;

        if (line.front() == ' ' || line.front() == '\t') {
            // Obsolete line folding (RFC 9112 section 5.2): a continuation of the previous value.
            const auto continuation = text::Trim(line);
            if (!headers.empty() && !continuation.empty()) {
                auto& value = headers.back().value;
                if (!value.empty()) value += ' ';
                value += continuation;
            }
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const auto name = text::Trim(line.substr(0, colon));
        if (name.empty()) continue;
        headers.push_back(HttpHeader{std::string(name), std::string(text::Trim(line.substr(colon + 1)))});
    }
    return headers;
}

const std::string* FindHeader(std::span<const HttpHeader> headers, std::string_view name) noexcept {
    for (const auto& header : headers) {
        if (text::EqualsIgnoreCaseAscii(header.name, name)) return &header.value;
    }
    return nullptr;
}

std::optional<std::uint64_t> ParseContentLength(std::span<const HttpHeader> headers) {
    std::optional<std::uint64_t> length;
    for (const auto& header : headers) {
        if (!text::EqualsIgnoreCaseAscii(header.name, "Content-Length")) continue;
        const auto value = ParseDecimal(header.value);
        if (!value) return std::nullopt;
        if (length && *length != *value) return std::nullopt;
        length = value;
    }
    return length;
}

bool IsWinHttpDecodedCoding(std::span<const HttpHeader> headers) noexcept {
    const std::string* coding = nullptr;
    for (const auto& header : headers) {
        if (!text::EqualsIgnoreCaseAscii(header.name, "Content-Encoding")) continue;
        if (coding) return false;  // several codings: WinHTTP passes the body through
        coding = &header.value;
    }
    if (!coding) return false;
    const auto value = text::Trim(std::string_view(*coding));
    return text::EqualsIgnoreCaseAscii(value, "gzip") || text::EqualsIgnoreCaseAscii(value, "deflate");
}

bool ResponseHasBody(std::string_view method, int status) noexcept {
    if (method == "HEAD") return false;
    return status >= 200 && status != 204 && status != 304;
}

bool IsCredentialHeader(std::string_view name) noexcept {
    return text::EqualsIgnoreCaseAscii(name, "Authorization") ||
           text::EqualsIgnoreCaseAscii(name, "Proxy-Authorization") || text::EqualsIgnoreCaseAscii(name, "Cookie");
}

}  // namespace aether::net
