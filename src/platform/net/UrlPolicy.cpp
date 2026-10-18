// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/net/UrlPolicy.h"

#include "core/Strings.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>

namespace aether::net {
namespace {

Error InvalidUrl(std::string_view reason) {
    return MakeError(ErrorCode::InvalidArgument, "invalid request URL: " + std::string(reason));
}

constexpr bool IsAsciiAlpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool IsAsciiDigit(char c) noexcept { return c >= '0' && c <= '9'; }

std::string_view StripBrackets(std::string_view host) noexcept {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') return host.substr(1, host.size() - 2);
    return host;
}

// Characters WinHttpCrackUrl would accept but that have no business in a URL we send: whitespace and
// control characters (request smuggling), backslashes (parsers disagree on them, e.g.
// "https://good\@evil/"), and non-ASCII (WinHTTP neither encodes IRIs nor converts IDN host names).
std::string_view FindForbiddenCharacter(std::string_view text) noexcept {
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7F) return "the URL contains whitespace or control characters";
        if (u >= 0x80) return "the URL must be ASCII (percent-encode other characters, use Punycode for host names)";
        if (c == '\\') return "the URL contains a backslash";
    }
    return {};
}

bool IsValidHostName(std::string_view host) noexcept {
    const bool ipv6 = host.find(':') != std::string_view::npos;
    for (const char c : host) {
        if (IsAsciiAlpha(c) || IsAsciiDigit(c) || c == '-' || c == '.' || c == '_') continue;
        if (ipv6 && c == ':') continue;
        return false;
    }
    return true;
}

// Splits a URI reference into its RFC 3986 components (the fragment is discarded).
struct Reference {
    std::string_view scheme;  // empty when the reference is relative
    bool hasAuthority = false;
    std::string_view authority;
    std::string_view path;
    bool hasQuery = false;
    std::string_view query;  // without the '?'
};

Reference SplitReference(std::string_view text) noexcept {
    Reference reference;
    text = text.substr(0, text.find('#'));

    const auto colon = text.find(':');
    if (colon != std::string_view::npos && colon > 0 && IsAsciiAlpha(text.front()) &&
        text.find_first_of("/?") > colon) {
        bool validScheme = true;
        for (std::size_t i = 1; i < colon; ++i) {
            const char c = text[i];
            if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != '+' && c != '-' && c != '.') validScheme = false;
        }
        if (validScheme) {
            reference.scheme = text.substr(0, colon);
            text.remove_prefix(colon + 1);
        }
    }
    if (text.starts_with("//")) {
        text.remove_prefix(2);
        const auto end = text.find_first_of("/?");
        reference.hasAuthority = true;
        reference.authority = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end);
    }
    const auto question = text.find('?');
    reference.path = text.substr(0, question);
    if (question != std::string_view::npos) {
        reference.hasQuery = true;
        reference.query = text.substr(question + 1);
    }
    return reference;
}

// RFC 3986 section 5.2.4.
std::string RemoveDotSegments(std::string_view path) {
    std::string input(path);
    std::string output;
    const auto dropLastSegment = [&output] {
        const auto slash = output.rfind('/');
        output.erase(slash == std::string::npos ? 0 : slash);
    };
    while (!input.empty()) {
        if (input.starts_with("../")) {
            input.erase(0, 3);
        } else if (input.starts_with("./")) {
            input.erase(0, 2);
        } else if (input.starts_with("/./")) {
            input.erase(0, 2);
        } else if (input == "/.") {
            input = "/";
        } else if (input.starts_with("/../")) {
            input.erase(0, 3);
            dropLastSegment();
        } else if (input == "/..") {
            input = "/";
            dropLastSegment();
        } else if (input == "." || input == "..") {
            input.clear();
        } else {
            const auto next = input.find('/', input.front() == '/' ? 1 : 0);
            output.append(input, 0, next);
            input.erase(0, next);
        }
    }
    return output;
}

}  // namespace

std::string ParsedUrl::ConnectHost() const {
    if (host.find(':') != std::string::npos) return "[" + host + "]";
    return host;
}

std::string ParsedUrl::Origin() const {
    std::string origin = secure ? "https://" : "http://";
    origin += ConnectHost();
    if (port != (secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT)) {
        origin += ':';
        origin += std::to_string(port);
    }
    return origin;
}

bool IsLoopbackHost(std::string_view host) noexcept {
    const std::string_view bare = StripBrackets(host);
    return text::EqualsIgnoreCaseAscii(bare, "localhost") || bare == "127.0.0.1" || bare == "::1";
}

Result<ParsedUrl> ParseRequestUrl(std::string_view url, bool allowInsecureLoopback) {
    if (url.empty()) return InvalidUrl("the URL is empty");
    if (url.size() > kMaxUrlLength) return InvalidUrl("the URL is longer than 8192 characters");
    if (const auto problem = FindForbiddenCharacter(url); !problem.empty()) return InvalidUrl(problem);

    // WinHttpCrackUrl reports "https://@host/" as having no user name, so look for the separator itself.
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) return InvalidUrl("the URL is not absolute");
    const auto authorityStart = schemeEnd + 3;
    const auto authority = url.substr(authorityStart, url.find_first_of("/?#", authorityStart) - authorityStart);
    if (authority.find('@') != std::string_view::npos) return InvalidUrl("user information is not allowed in URLs");

    const std::wstring wide = text::Widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    // (DWORD)-1 asks WinHttpCrackUrl for pointers into `wide` instead of copies.
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = static_cast<DWORD>(-1);
    parts.dwPasswordLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!::WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), ICU_REJECT_USERPWD, &parts)) {
        const DWORD error = ::GetLastError();
        Error failure = InvalidUrl(error == ERROR_WINHTTP_UNRECOGNIZED_SCHEME ? "only https URLs are supported"
                                                                              : "the URL could not be parsed");
        failure.platformCode = static_cast<std::int64_t>(error);
        return failure;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP) {
        return InvalidUrl("only https URLs are supported");
    }
    if (parts.dwUserNameLength > 0 || parts.dwPasswordLength > 0) {
        return InvalidUrl("user information is not allowed in URLs");
    }

    ParsedUrl parsed;
    parsed.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring_view wideHost(parts.lpszHostName ? parts.lpszHostName : L"", parts.dwHostNameLength);
    parsed.host = std::string(StripBrackets(text::Narrow(wideHost)));
    if (parsed.host.empty()) return InvalidUrl("the URL has no host");
    if (!IsValidHostName(parsed.host)) return InvalidUrl("the host name contains characters that are not allowed");
    if (parts.nPort == 0) return InvalidUrl("port 0 is not a valid destination");
    parsed.port = parts.nPort;

    if (!parsed.secure) {
        if (!allowInsecureLoopback) return InvalidUrl("plain http is not allowed; use https");
        if (!IsLoopbackHost(parsed.host)) return InvalidUrl("plain http is only allowed for loopback hosts");
    }

    std::wstring_view widePath(parts.lpszUrlPath ? parts.lpszUrlPath : L"", parts.dwUrlPathLength);
    std::wstring_view wideExtra(parts.lpszExtraInfo ? parts.lpszExtraInfo : L"", parts.dwExtraInfoLength);
    std::string target = text::Narrow(widePath) + text::Narrow(wideExtra);
    target.erase(std::min(target.find('#'), target.size()));  // fragments are never sent
    if (target.empty() || target.front() != '/') target.insert(target.begin(), '/');
    parsed.pathAndQuery = std::move(target);
    return parsed;
}

bool IsRedirectStatus(int status) noexcept {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool IsRedirectHostAllowed(std::string_view host, std::span<const std::string> allowedHosts) noexcept {
    const std::string_view bare = StripBrackets(host);
    if (bare.empty()) return false;
    for (const auto& allowed : allowedHosts) {
        if (text::EqualsIgnoreCaseAscii(StripBrackets(allowed), bare)) return true;
    }
    return false;
}

Result<std::string> ResolveReference(std::string_view baseUrl, std::string_view reference) {
    reference = text::Trim(reference);
    if (reference.empty()) return MakeError(ErrorCode::InvalidArgument, "the redirect location is empty");
    if (reference.size() > kMaxUrlLength || !FindForbiddenCharacter(reference).empty()) {
        return MakeError(ErrorCode::InvalidArgument, "the redirect location contains characters that are not allowed");
    }

    const Reference base = SplitReference(baseUrl);
    if (base.scheme.empty() || !base.hasAuthority) {
        return MakeError(ErrorCode::InvalidArgument, "the base URL is not absolute");
    }
    const Reference ref = SplitReference(reference);

    std::string_view scheme = base.scheme;
    std::string_view authority = base.authority;
    std::string path;
    bool hasQuery = ref.hasQuery;
    std::string_view query = ref.query;

    if (!ref.scheme.empty()) {
        if (!ref.hasAuthority) {
            return MakeError(ErrorCode::InvalidArgument, "the redirect location is not a hierarchical URL");
        }
        scheme = ref.scheme;
        authority = ref.authority;
        path = RemoveDotSegments(ref.path);
    } else if (ref.hasAuthority) {
        authority = ref.authority;
        path = RemoveDotSegments(ref.path);
    } else if (ref.path.empty()) {
        path = std::string(base.path);
        if (!ref.hasQuery) {
            hasQuery = base.hasQuery;
            query = base.query;
        }
    } else if (ref.path.front() == '/') {
        path = RemoveDotSegments(ref.path);
    } else {
        // Merge with the base path: everything up to and including its last '/'.
        std::string merged = base.path.empty() ? std::string("/")
                                               : std::string(base.path.substr(0, base.path.rfind('/') + 1));
        merged += ref.path;
        path = RemoveDotSegments(merged);
    }

    std::string resolved;
    resolved.reserve(scheme.size() + authority.size() + path.size() + query.size() + 5);
    resolved.append(scheme).append("://").append(authority).append(path);
    if (hasQuery) resolved.append("?").append(query);
    return resolved;
}

Result<ParsedUrl> EvaluateRedirect(const ParsedUrl& from, std::string_view targetUrl,
                                   std::span<const std::string> allowedHosts, bool allowInsecureLoopback) {
    auto target = ParseRequestUrl(targetUrl, allowInsecureLoopback);
    if (!target) {
        return MakeError(ErrorCode::Validation, "redirect to an invalid or insecure URL rejected (" +
                                                    target.error().message + ")");
    }
    if (from.secure && !target->secure) {
        return MakeError(ErrorCode::Validation, "redirect from https to plain http rejected");
    }
    if (!IsRedirectHostAllowed(target->host, allowedHosts)) {
        std::string message = "redirect to untrusted host '" + text::SanitizeForLog(target->host, 255) + "' rejected";
        if (allowedHosts.empty()) message += " (this request allows no redirects)";
        return MakeError(ErrorCode::Validation, std::move(message));
    }
    return target;
}

}  // namespace aether::net
