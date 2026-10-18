// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/KeyboardLayouts.h"

#include "core/Strings.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"

#include <windows.h>
#include <shlwapi.h>

#include <array>
#include <cstdlib>
#include <cwctype>
#include <format>

namespace aether::status {
namespace {

constexpr wchar_t kKeyboardLayoutsKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";

// GetLocaleInfoEx for a string value; empty when the locale or the value is unknown.
std::wstring LocaleText(const wchar_t* localeName, LCTYPE type) {
    const int size = ::GetLocaleInfoEx(localeName, type, nullptr, 0);
    if (size <= 1) return {};
    std::wstring value(static_cast<std::size_t>(size), L'\0');
    const int written = ::GetLocaleInfoEx(localeName, type, value.data(), size);
    if (written <= 1) return {};
    value.resize(static_cast<std::size_t>(written - 1));  // drop the terminator
    return value;
}

std::wstring UpperAscii(std::wstring text) {
    for (auto& c : text) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    return text;
}

bool IsHexKlid(std::wstring_view klid) noexcept {
    if (klid.size() != 8) return false;
    for (const wchar_t c : klid) {
        if (!std::iswxdigit(c)) return false;
    }
    return true;
}

std::wstring Hex8(std::uint32_t value) { return std::format(L"{:08X}", value); }

// Reads a REG_SZ / REG_EXPAND_SZ value (expanded). std::nullopt when absent or empty.
std::optional<std::wstring> ReadRegistryString(HKEY root, const std::wstring& subKey, const wchar_t* valueName) {
    DWORD bytes = 0;
    LSTATUS status = ::RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr,
                                    nullptr, &bytes);
    // Layout names are short; anything larger than this is not a value we want to show.
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t) || bytes > 4096) return std::nullopt;
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    status = ::RegGetValueW(root, subKey.c_str(), valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr,
                            value.data(), &bytes);
    if (status != ERROR_SUCCESS) return std::nullopt;
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    if (value.empty()) return std::nullopt;
    return value;
}

}  // namespace

Result<InputLanguageInfo> DescribeInputLanguage(std::uint16_t languageId) {
    if (languageId == 0) return MakeError(ErrorCode::InvalidArgument, "input language id is 0");
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};
    // Transient LANGIDs (0x2000, 0x2400, ...) assigned to input languages without a fixed LCID are
    // resolved for the current user by LCIDToLocaleName as well.
    const int length = ::LCIDToLocaleName(MAKELCID(languageId, SORT_DEFAULT), name.data(),
                                          static_cast<int>(name.size()), LOCALE_ALLOW_NEUTRAL_NAMES);
    if (length <= 1) return platform::LastWin32Error(std::format("LCIDToLocaleName(0x{:04X})", languageId));
    InputLanguageInfo info;
    info.localeName.assign(name.data(), static_cast<std::size_t>(length - 1));
    info.shortLabel = ShortLanguageLabel(info.localeName);
    info.languageName = LocaleText(info.localeName.c_str(), LOCALE_SLOCALIZEDDISPLAYNAME);
    if (info.shortLabel.empty()) {
        return MakeError(ErrorCode::NotFound,
                         "Windows has no ISO language code for input locale " + text::Narrow(info.localeName));
    }
    return info;
}

std::wstring ShortLanguageLabel(std::wstring_view localeName) {
    if (localeName.empty()) return {};
    const std::wstring locale(localeName);
    std::wstring code = LocaleText(locale.c_str(), LOCALE_SISO639LANGNAME);
    if (code.size() != 2) {
        // No two-letter code: Windows returns the three-letter one in LOCALE_SISO639LANGNAME, but
        // LOCALE_SISO639LANGNAME2 is the documented source for it.
        std::wstring threeLetter = LocaleText(locale.c_str(), LOCALE_SISO639LANGNAME2);
        if (!threeLetter.empty()) code = std::move(threeLetter);
    }
    return UpperAscii(std::move(code));
}

std::optional<std::wstring> KeyboardLayoutIdFromHkl(std::uintptr_t hkl, const LayoutIdResolver& resolver) {
    // Only the low 32 bits are meaningful; on x64 the handle value may be sign-extended.
    const auto value = static_cast<std::uint32_t>(hkl & 0xFFFF'FFFFu);
    const auto device = static_cast<std::uint16_t>(value >> 16);
    switch (device & 0xF000u) {
        case 0xF000u: {
            // Layout variant (for example "United States-Dvorak"): look up the KLID by its Layout Id.
            if (!resolver) return std::nullopt;
            auto klid = resolver(static_cast<std::uint16_t>(device & 0x0FFFu));
            if (klid && !IsHexKlid(*klid)) return std::nullopt;
            return klid;
        }
        case 0xE000u: return Hex8(value);  // legacy IMM32 IME: the KLID is the whole HKL
        default:
            if (device == 0) return std::nullopt;
            return Hex8(device);  // the layout is the device handle's primary language
    }
}

std::optional<std::wstring> FindKeyboardLayoutIdByLayoutId(std::uint16_t layoutId) {
    platform::UniqueRegKey root;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKeyboardLayoutsKey, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                        root.Put()) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    for (DWORD index = 0;; ++index) {
        std::array<wchar_t, 64> subKey{};
        DWORD subKeyLength = static_cast<DWORD>(subKey.size());
        const LSTATUS status =
            ::RegEnumKeyExW(root.Get(), index, subKey.data(), &subKeyLength, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) continue;  // ERROR_MORE_DATA: not a KLID-shaped name
        const std::wstring klid(subKey.data(), subKeyLength);
        if (!IsHexKlid(klid)) continue;
        const auto id = ReadRegistryString(root.Get(), klid, L"Layout Id");
        if (!id) continue;
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(id->c_str(), &end, 16);
        if (end != id->c_str() && *end == L'\0' && parsed == layoutId) return UpperAscii(klid);
    }
    return std::nullopt;
}

std::optional<std::wstring> KeyboardLayoutDisplayName(std::wstring_view klid) {
    if (!IsHexKlid(klid)) return std::nullopt;
    const std::wstring subKey = std::wstring(kKeyboardLayoutsKey) + L"\\" + std::wstring(klid);

    // "Layout Display Name" is an indirect string ("@%SystemRoot%\system32\input.dll,-5000") localized
    // in the Windows display language. RegGetValue already expanded the environment variables.
    if (auto indirect = ReadRegistryString(HKEY_LOCAL_MACHINE, subKey, L"Layout Display Name");
        indirect && indirect->front() == L'@') {
        std::array<wchar_t, 256> resolved{};
        if (SUCCEEDED(::SHLoadIndirectString(indirect->c_str(), resolved.data(), static_cast<UINT>(resolved.size()),
                                             nullptr)) &&
            resolved[0] != L'\0') {
            return std::wstring(resolved.data());
        }
    }
    // Not localized, but always present for registered layouts.
    return ReadRegistryString(HKEY_LOCAL_MACHINE, subKey, L"Layout Text");
}

KeyboardSnapshot DescribeKeyboardLayout(std::uintptr_t hkl) {
    KeyboardSnapshot snapshot;
    const auto languageId = static_cast<std::uint16_t>(hkl & 0xFFFFu);
    auto language = DescribeInputLanguage(languageId);
    if (!language) {
        snapshot.state = ProviderState::Error;
        snapshot.detail = std::format("cannot identify input language 0x{:04X}: {}", languageId,
                                      language.error().Describe());
        return snapshot;
    }
    snapshot.state = ProviderState::Available;
    snapshot.localeName = std::move(language.value().localeName);
    snapshot.shortLabel = std::move(language.value().shortLabel);
    snapshot.languageName = std::move(language.value().languageName);
    if (auto klid = KeyboardLayoutIdFromHkl(hkl, FindKeyboardLayoutIdByLayoutId)) {
        if (auto name = KeyboardLayoutDisplayName(*klid)) snapshot.layoutName = std::move(*name);
    }
    return snapshot;
}

}  // namespace aether::status
