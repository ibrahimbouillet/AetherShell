// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/ReleaseNotes.h"

#include <cstdint>
#include <vector>

namespace aether::updates {
namespace {

constexpr char32_t kReplacement = U'\xFFFD';  // U+FFFD REPLACEMENT CHARACTER
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // U+2026

// Decodes one UTF-8 code point starting at `i` (advancing it). Invalid sequences (overlong forms,
// surrogates, values above U+10FFFF, truncation) yield U+FFFD and consume one byte.
char32_t DecodeOne(std::string_view text, std::size_t& i) noexcept {
    const auto lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
        ++i;
        return lead;
    }
    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1F;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0F;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07;
        minimum = 0x10000;
    } else {
        ++i;
        return kReplacement;
    }
    if (i + length > text.size()) {
        ++i;
        return kReplacement;
    }
    for (std::size_t k = 1; k < length; ++k) {
        const auto next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) {
            ++i;
            return kReplacement;
        }
        value = (value << 6) | (next & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        ++i;
        return kReplacement;
    }
    i += length;
    return value;
}

void AppendUtf8(std::string& out, char32_t c) {
    const auto v = static_cast<std::uint32_t>(c);
    if (v < 0x80) {
        out += static_cast<char>(v);
    } else if (v < 0x800) {
        out += static_cast<char>(0xC0 | (v >> 6));
        out += static_cast<char>(0x80 | (v & 0x3F));
    } else if (v < 0x10000) {
        out += static_cast<char>(0xE0 | (v >> 12));
        out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (v & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (v >> 18));
        out += static_cast<char>(0x80 | ((v >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (v & 0x3F));
    }
}

bool IsRemoved(char32_t c) noexcept {
    if (c < 0x20) return c != U'\t';  // LF is handled before this check
    if (c == 0x7F) return true;
    if (c >= 0x80 && c <= 0x9F) return true;           // C1 controls
    if (c >= 0x202A && c <= 0x202E) return true;       // LRE RLE PDF LRO RLO
    if (c >= 0x2066 && c <= 0x2069) return true;       // LRI RLI FSI PDI
    if (c == 0x200E || c == 0x200F) return true;       // LRM RLM
    if (c == 0xFEFF) return true;                      // BOM / zero-width no-break space
    if (c >= 0xFFF9 && c <= 0xFFFB) return true;       // interlinear annotation controls
    return false;
}

void TrimTrailingBlanks(std::string& line) {
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
}

}  // namespace

std::string SanitizeReleaseNotes(std::string_view untrusted, std::size_t maxBytes) {
    // 1. Decode, normalize line breaks and drop unwanted characters, splitting into lines.
    std::vector<std::string> lines(1);
    for (std::size_t i = 0; i < untrusted.size();) {
        char32_t c = DecodeOne(untrusted, i);
        if (c == U'\r') {
            if (i < untrusted.size() && untrusted[i] == '\n') ++i;  // CRLF
            c = U'\n';
        }
        if (c == U'\n' || c == 0x2028 || c == 0x2029) {
            TrimTrailingBlanks(lines.back());
            lines.emplace_back();
            continue;
        }
        if (IsRemoved(c)) continue;
        AppendUtf8(lines.back(), c);
    }
    TrimTrailingBlanks(lines.back());

    // 2. Reassemble: no leading/trailing blank lines, at most one blank line in a row.
    std::string result;
    bool pendingBlank = false;
    for (const auto& line : lines) {
        if (line.empty()) {
            pendingBlank = !result.empty();
            continue;
        }
        if (!result.empty()) result += pendingBlank ? "\n\n" : "\n";
        pendingBlank = false;
        result += line;
    }

    // 3. Cap the length on a code-point boundary.
    if (result.size() > maxBytes) {
        const bool roomForEllipsis = maxBytes >= kEllipsis.size();
        std::size_t cut = roomForEllipsis ? maxBytes - kEllipsis.size() : maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0) == 0x80) --cut;
        result.resize(cut);
        TrimTrailingBlanks(result);
        while (!result.empty() && result.back() == '\n') result.pop_back();
        if (roomForEllipsis) result += kEllipsis;
    }
    return result;
}

}  // namespace aether::updates
