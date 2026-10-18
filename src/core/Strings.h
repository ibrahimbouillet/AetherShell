// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Text conventions:
//  * std::string holds UTF-8 (logs, JSON, network data).
//  * std::wstring holds UTF-16 (Win32 APIs, UI text).
// Conversions never throw; malformed input is replaced with U+FFFD.
#pragma once

#include <string>
#include <string_view>

namespace aether::text {

[[nodiscard]] std::wstring Widen(std::string_view utf8);
[[nodiscard]] std::string Narrow(std::wstring_view utf16);
[[nodiscard]] bool IsValidUtf8(std::string_view bytes) noexcept;

// Locale-independent lowercase mapping (Unicode simple case mapping via the invariant locale).
[[nodiscard]] std::wstring ToLower(std::wstring_view text);
[[nodiscard]] std::string ToLowerAscii(std::string_view text);

// Ordinal, case-insensitive comparison (no linguistic rules; suitable for identifiers and paths).
[[nodiscard]] bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept;
[[nodiscard]] bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b) noexcept;

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept;
[[nodiscard]] std::wstring_view Trim(std::wstring_view text) noexcept;

// Replaces control characters (including CR/LF) with spaces so untrusted text cannot forge log lines.
[[nodiscard]] std::string SanitizeForLog(std::string_view text, std::size_t maxLength = 512);

}  // namespace aether::text
