// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/Encoding.h"

#include <array>
#include <format>

namespace aether::updates::encoding {
namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int HexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr std::array<int, 256> MakeBase64Table() {
    std::array<int, 256> table{};
    for (auto& entry : table) entry = -1;
    for (std::size_t i = 0; i < kBase64Alphabet.size(); ++i) {
        table[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<int>(i);
    }
    return table;
}
constexpr auto kBase64Table = MakeBase64Table();

Error Base64Error(std::string_view reason) { return MakeError(ErrorCode::Parse, "invalid base64: " + std::string(reason)); }

// Parses exactly `width` ASCII digits.
bool ParseDigits(std::string_view text, std::size_t offset, std::size_t width, int& value) noexcept {
    value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        const char c = text[offset + i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    return true;
}

}  // namespace

std::string ToHex(std::span<const std::uint8_t> bytes) {
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text += kHexDigits[byte >> 4];
        text += kHexDigits[byte & 0x0F];
    }
    return text;
}

Result<std::vector<std::uint8_t>> FromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) return MakeError(ErrorCode::Parse, "hex text has an odd number of digits");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int high = HexValue(hex[i]);
        const int low = HexValue(hex[i + 1]);
        if (high < 0 || low < 0) return MakeError(ErrorCode::Parse, "hex text contains a non-hex character");
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

Result<std::vector<std::uint8_t>> DecodeBase64(std::string_view text) {
    if (text.size() % 4 != 0) return Base64Error("length is not a multiple of 4");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        int values[4] = {};
        int padding = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=') {
                // Padding may only fill the last one or two positions of the final quantum.
                if (!last || k < 2) return Base64Error("misplaced padding");
                ++padding;
                values[k] = 0;
                continue;
            }
            if (padding > 0) return Base64Error("data after padding");
            values[k] = kBase64Table[static_cast<unsigned char>(c)];
            if (values[k] < 0) return Base64Error("character outside the base64 alphabet");
        }
        const std::uint32_t quantum = (static_cast<std::uint32_t>(values[0]) << 18) |
                                      (static_cast<std::uint32_t>(values[1]) << 12) |
                                      (static_cast<std::uint32_t>(values[2]) << 6) |
                                      static_cast<std::uint32_t>(values[3]);
        bytes.push_back(static_cast<std::uint8_t>(quantum >> 16));
        if (padding == 2) {
            if ((values[1] & 0x0F) != 0) return Base64Error("non-canonical trailing bits");
            continue;
        }
        bytes.push_back(static_cast<std::uint8_t>((quantum >> 8) & 0xFF));
        if (padding == 1) {
            if ((values[2] & 0x03) != 0) return Base64Error("non-canonical trailing bits");
            continue;
        }
        bytes.push_back(static_cast<std::uint8_t>(quantum & 0xFF));
    }
    return bytes;
}

std::string EncodeBase64(std::span<const std::uint8_t> bytes) {
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t quantum = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                      (static_cast<std::uint32_t>(bytes[i + 1]) << 8) | bytes[i + 2];
        text += kBase64Alphabet[(quantum >> 18) & 0x3F];
        text += kBase64Alphabet[(quantum >> 12) & 0x3F];
        text += kBase64Alphabet[(quantum >> 6) & 0x3F];
        text += kBase64Alphabet[quantum & 0x3F];
    }
    const std::size_t remaining = bytes.size() - i;
    if (remaining > 0) {
        std::uint32_t quantum = static_cast<std::uint32_t>(bytes[i]) << 16;
        if (remaining == 2) quantum |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
        text += kBase64Alphabet[(quantum >> 18) & 0x3F];
        text += kBase64Alphabet[(quantum >> 12) & 0x3F];
        text += remaining == 2 ? kBase64Alphabet[(quantum >> 6) & 0x3F] : '=';
        text += '=';
    }
    return text;
}

std::string FormatUtcTimestamp(std::chrono::system_clock::time_point time) {
    using namespace std::chrono;
    const auto seconds = floor<std::chrono::seconds>(time);
    const auto days = floor<std::chrono::days>(seconds);
    const year_month_day date{days};
    const hh_mm_ss clock{seconds - days};
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()), clock.hours().count(),
                       clock.minutes().count(), clock.seconds().count());
}

Result<std::chrono::system_clock::time_point> ParseUtcTimestamp(std::string_view text) {
    using namespace std::chrono;
    // 0123456789012345678 9
    // YYYY-MM-DDTHH:MM:SS Z
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z') {
        return MakeError(ErrorCode::Parse, "timestamp is not in the form YYYY-MM-DDTHH:MM:SSZ");
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (!ParseDigits(text, 0, 4, y) || !ParseDigits(text, 5, 2, mo) || !ParseDigits(text, 8, 2, d) ||
        !ParseDigits(text, 11, 2, h) || !ParseDigits(text, 14, 2, mi) || !ParseDigits(text, 17, 2, s)) {
        return MakeError(ErrorCode::Parse, "timestamp contains a non-digit");
    }
    const year_month_day date{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (y < 1970 || !date.ok() || h > 23 || mi > 59 || s > 59) {
        return MakeError(ErrorCode::Parse, "timestamp is out of range");
    }
    return system_clock::time_point{sys_days{date} + hours{h} + minutes{mi} + seconds{s}};
}

}  // namespace aether::updates::encoding
