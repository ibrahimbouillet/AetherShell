// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Describes an input locale handle (HKL) for the status bar: language tag, short label, localized
// language name and keyboard layout name. Everything here is a function of the HKL value and the
// Windows locale/registry data, so it is unit tested with fixed handles.
//
// HKL anatomy: LOWORD is the input language (LANGID); HIWORD is the "device handle":
//  * 0xFxxx  -> a layout variant; the low 12 bits are the registry "Layout Id" of the KLID,
//  * 0xExxx  -> a legacy IMM32 IME; the KLID is the whole HKL value,
//  * other   -> the layout's primary language; the KLID is that value zero-extended to 8 digits.
#pragma once

#include "core/Result.h"
#include "status/StatusTypes.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace aether::status {

struct InputLanguageInfo {
    std::wstring localeName;    // "fr-FR"
    std::wstring shortLabel;    // "FR"
    std::wstring languageName;  // "French (France)", localized in the Windows display language
};

// Language part of an HKL (its LANGID). Fails when Windows has no locale name for it.
[[nodiscard]] Result<InputLanguageInfo> DescribeInputLanguage(std::uint16_t languageId);

// Upper-case ISO 639-1 code of a locale ("fr-FR" -> "FR"); the ISO 639-2 code when the language has
// no two-letter code ("haw-US" -> "HAW"). Empty when the locale is unknown.
[[nodiscard]] std::wstring ShortLanguageLabel(std::wstring_view localeName);

// Resolves a 12-bit "Layout Id" to its KLID. The production resolver scans the registry.
using LayoutIdResolver = std::function<std::optional<std::wstring>(std::uint16_t layoutId)>;

// KLID (keyboard layout identifier, 8 upper-case hex digits) of an HKL, or std::nullopt when it
// cannot be derived.
[[nodiscard]] std::optional<std::wstring> KeyboardLayoutIdFromHkl(std::uintptr_t hkl, const LayoutIdResolver& resolver);

// Registry scan of HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layouts for "Layout Id" == layoutId.
[[nodiscard]] std::optional<std::wstring> FindKeyboardLayoutIdByLayoutId(std::uint16_t layoutId);

// Best-effort localized layout name for a KLID: "Layout Display Name" resolved with SHLoadIndirectString,
// else "Layout Text". std::nullopt when the layout is not registered.
[[nodiscard]] std::optional<std::wstring> KeyboardLayoutDisplayName(std::wstring_view klid);

// Full description of an HKL as an Available snapshot (fromForegroundWindow left false), or an Error
// snapshot with a detail message when the language cannot be identified.
[[nodiscard]] KeyboardSnapshot DescribeKeyboardLayout(std::uintptr_t hkl);

}  // namespace aether::status
