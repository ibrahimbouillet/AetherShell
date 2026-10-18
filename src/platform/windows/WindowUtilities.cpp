// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/WindowUtilities.h"

#include "platform/windows/Win32Error.h"

#include <dwmapi.h>

#include <iterator>
#include <optional>

namespace aether::platform {
namespace {

// Defined in newer SDKs; spelled out so older SDKs still compile.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;       // DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kDwmWindowCornerPreference = 33;     // DWMWA_WINDOW_CORNER_PREFERENCE

std::optional<DWORD> ReadPersonalizeValue(const wchar_t* name) noexcept {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER,
                                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name,
                                          RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (status != ERROR_SUCCESS) return std::nullopt;
    return value;
}

}  // namespace

std::wstring WindowClassName(HWND hwnd) {
    wchar_t buffer[256];
    const int length = ::GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, static_cast<size_t>(length)) : std::wstring();
}

std::wstring WindowTitle(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize(static_cast<size_t>(copied > 0 ? copied : 0));
    return title;
}

bool IsWindowCloaked(HWND hwnd) noexcept {
    DWORD cloaked = 0;
    return SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

void ApplyImmersiveDarkMode(HWND hwnd, bool dark) noexcept {
    const BOOL value = dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode, &value, sizeof(value));
}

void SetCornerPreference(HWND hwnd, CornerPreference preference) noexcept {
    const DWORD value = static_cast<DWORD>(preference);
    ::DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference, &value, sizeof(value));
}

void ExcludeFromPeek(HWND hwnd) noexcept {
    const BOOL value = TRUE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &value, sizeof(value));
}

bool SystemPrefersDarkApps() noexcept {
    const auto value = ReadPersonalizeValue(L"AppsUseLightTheme");
    return value.has_value() && *value == 0;
}

bool SystemTransparencyEnabled() noexcept {
    const auto value = ReadPersonalizeValue(L"EnableTransparency");
    return !value.has_value() || *value != 0;
}

bool HighContrastEnabled() noexcept {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool SystemAnimationsEnabled() noexcept {
    BOOL enabled = TRUE;
    if (!::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return true;
    return enabled != FALSE;
}

Result<ATOM> RegisterWindowClassOnce(const WNDCLASSEXW& windowClass) {
    WNDCLASSEXW existing{sizeof(existing)};
    if (const ATOM atom = static_cast<ATOM>(
            ::GetClassInfoExW(windowClass.hInstance, windowClass.lpszClassName, &existing))) {
        return atom;
    }
    const ATOM atom = ::RegisterClassExW(&windowClass);
    if (!atom) {
        if (::GetLastError() == ERROR_CLASS_ALREADY_EXISTS) return ATOM{1};
        return LastWin32Error("RegisterClassEx");
    }
    return atom;
}

}  // namespace aether::platform
