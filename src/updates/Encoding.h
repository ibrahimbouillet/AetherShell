// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Strict text encodings used by the update trust chain: hex (keys, digests),
// base64 (signatures) and UTC timestamps (GitHub metadata, state file).
#pragma once

#include "core/Result.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::updates::encoding {

// Lowercase hex.
[[nodiscard]] std::string ToHex(std::span<const std::uint8_t> bytes);
// Even number of [0-9A-Fa-f] characters, nothing else. Errors use ErrorCode::Parse.
[[nodiscard]] Result<std::vector<std::uint8_t>> FromHex(std::string_view hex);

// RFC 4648 section 4 base64 with the standard alphabet. Strict: padding is required, no whitespace
// or line breaks, '=' only at the end, and unused bits of the last character must be zero (so every
// byte string has exactly one accepted encoding). Errors use ErrorCode::Parse.
[[nodiscard]] Result<std::vector<std::uint8_t>> DecodeBase64(std::string_view text);
[[nodiscard]] std::string EncodeBase64(std::span<const std::uint8_t> bytes);

// "YYYY-MM-DDTHH:MM:SSZ" (UTC, whole seconds), the format GitHub uses for timestamps.
[[nodiscard]] std::string FormatUtcTimestamp(std::chrono::system_clock::time_point time);
[[nodiscard]] Result<std::chrono::system_clock::time_point> ParseUtcTimestamp(std::string_view text);

}  // namespace aether::updates::encoding
