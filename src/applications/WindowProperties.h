// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Shell properties that a top-level window declares about itself through its property store
// (SHGetPropertyStoreForWindow): the window's AppUserModelID and the optional relaunch icon and
// display name that the Windows taskbar shows for the window's group (browsers set them per
// profile). Only the window's own documented properties are read; the store is never written, and
// no browser files, profile databases or account data are read.
//
// The relaunch values can contain user-profile paths or a person's chosen profile name: never log
// them or write them to test output.
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace aether::apps {

inline constexpr std::size_t kMaxRelaunchDisplayNameLength = 128;

struct WindowShellProperties {
    std::wstring aumid;                 // System.AppUserModel.ID, raw (the identity rule validates it)
    std::wstring relaunchIconResource;  // System.AppUserModel.RelaunchIconResource, see SanitizeRelaunchIconResource
    std::wstring relaunchDisplayName;   // System.AppUserModel.RelaunchDisplayNameResource, see SanitizeRelaunchDisplayName
};

// Pure. The display name is kept only when it is plain text: trimmed, non-empty, at most
// kMaxRelaunchDisplayNameLength code units, without control characters and not an indirect string
// (a leading '@' names a resource that would have to be loaded from a file). Otherwise empty.
[[nodiscard]] std::wstring SanitizeRelaunchDisplayName(std::wstring_view raw);

// Pure. The icon resource is kept raw (it is parsed only when an icon is extracted, see
// IconResource.h) when it is non-empty after trimming, at most kMaxIconResourceLength code units
// and free of control characters. Otherwise empty.
[[nodiscard]] std::wstring SanitizeRelaunchIconResource(std::wstring_view raw);

// Reads the three properties from one property store of `window`, applying the sanitizers above.
// Call on a COM STA (the UI thread of the window tracker): without COM the store exposes no
// AppUserModelID. Missing properties are empty. InvalidArgument for a null or destroyed window,
// Platform when the shell gives no property store.
[[nodiscard]] Result<WindowShellProperties> ReadWindowShellProperties(HWND window);

}  // namespace aether::apps
