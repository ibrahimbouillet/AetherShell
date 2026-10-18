// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Strings.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cwctype>

namespace aether::text {

std::wstring Widen(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > static_cast<size_t>(INT_MAX)) return {};
    const int length = static_cast<int>(utf8.size());
    const int required = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<size_t>(required), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, result.data(), required);
    return result;
}

std::string Narrow(std::wstring_view utf16) {
    if (utf16.empty() || utf16.size() > static_cast<size_t>(INT_MAX)) return {};
    const int length = static_cast<int>(utf16.size());
    const int required = ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), length, nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<size_t>(required), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), length, result.data(), required, nullptr, nullptr);
    return result;
}

bool IsValidUtf8(std::string_view bytes) noexcept {
    size_t i = 0;
    const size_t n = bytes.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        int extra = 0;
        char32_t codePoint = 0;
        char32_t minimum = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            codePoint = c & 0x1F;
            minimum = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            codePoint = c & 0x0F;
            minimum = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            codePoint = c & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (i + static_cast<size_t>(extra) >= n) return false;
        for (int k = 1; k <= extra; ++k) {
            const auto cc = static_cast<unsigned char>(bytes[i + static_cast<size_t>(k)]);
            if ((cc & 0xC0) != 0x80) return false;
            codePoint = (codePoint << 6) | (cc & 0x3F);
        }
        if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            return false;
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return true;
}

std::wstring ToLower(std::wstring_view text) {
    if (text.empty() || text.size() > static_cast<size_t>(INT_MAX)) return std::wstring(text);
    const int length = static_cast<int>(text.size());
    const int required =
        ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), length, nullptr, 0, nullptr, nullptr, 0);
    if (required <= 0) return std::wstring(text);
    std::wstring result(static_cast<size_t>(required), L'\0');
    ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), length, result.data(), required, nullptr,
                    nullptr, 0);
    return result;
}

std::string ToLowerAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return result;
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    if (a.size() > static_cast<size_t>(INT_MAX)) return false;
    return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

std::string_view Trim(std::string_view text) noexcept {
    constexpr std::string_view kWhitespace = " \t\r\n\f\v";
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

std::wstring_view Trim(std::wstring_view text) noexcept {
    size_t first = 0;
    while (first < text.size() && std::iswspace(text[first])) ++first;
    size_t last = text.size();
    while (last > first && std::iswspace(text[last - 1])) --last;
    return text.substr(first, last - first);
}

std::string SanitizeForLog(std::string_view text, std::size_t maxLength) {
    std::string result;
    result.reserve(std::min(text.size(), maxLength));
    for (char c : text) {
        if (result.size() >= maxLength) {
            result += "...";
            break;
        }
        const auto u = static_cast<unsigned char>(c);
        result.push_back((u < 0x20 || u == 0x7F) ? ' ' : c);
    }
    return result;
}

}  // namespace aether::text
