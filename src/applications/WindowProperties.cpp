// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/WindowProperties.h"

#include "applications/IconResource.h"
#include "applications/ShellItems.h"
#include "core/Strings.h"
#include "platform/windows/Win32Error.h"

#include <propkey.h>
#include <shellapi.h>

#include <algorithm>

namespace aether::apps {
namespace {

bool HasControlCharacters(std::wstring_view value) noexcept {
    return std::any_of(value.begin(), value.end(), [](wchar_t c) { return c < 0x20 || c == 0x7F; });
}

}  // namespace

// Control characters are checked before trimming: a line break or tab anywhere disqualifies a value.
std::wstring SanitizeRelaunchDisplayName(std::wstring_view raw) {
    if (HasControlCharacters(raw)) return {};
    const auto trimmed = text::Trim(raw);
    if (trimmed.empty() || trimmed.size() > kMaxRelaunchDisplayNameLength || trimmed.front() == L'@') return {};
    return std::wstring(trimmed);
}

std::wstring SanitizeRelaunchIconResource(std::wstring_view raw) {
    if (HasControlCharacters(raw)) return {};
    const auto trimmed = text::Trim(raw);
    if (trimmed.empty() || trimmed.size() > kMaxIconResourceLength) return {};
    return std::wstring(trimmed);
}

Result<WindowShellProperties> ReadWindowShellProperties(HWND window) {
    if (!window || !::IsWindow(window)) return MakeError(ErrorCode::InvalidArgument, "not a window");
    try {
        winrt::com_ptr<IPropertyStore> store;
        const HRESULT hr = ::SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(store.put()));
        if (FAILED(hr) || !store) {
            return platform::ErrorFromHResult(FAILED(hr) ? hr : E_FAIL, "reading the window's shell properties");
        }
        WindowShellProperties properties;
        properties.aumid = shell::GetStringProperty(store.get(), PKEY_AppUserModel_ID).value_or(std::wstring{});
        if (auto icon = shell::GetStringProperty(store.get(), PKEY_AppUserModel_RelaunchIconResource)) {
            properties.relaunchIconResource = SanitizeRelaunchIconResource(*icon);
        }
        if (auto name = shell::GetStringProperty(store.get(), PKEY_AppUserModel_RelaunchDisplayNameResource)) {
            properties.relaunchDisplayName = SanitizeRelaunchDisplayName(*name);
        }
        return properties;
    } catch (...) {
        return shell::CurrentExceptionToError("reading the window's shell properties");
    }
}

}  // namespace aether::apps
