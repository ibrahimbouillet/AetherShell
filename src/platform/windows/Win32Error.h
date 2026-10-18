// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace aether::platform {

// System message text for a Win32 error code or HRESULT (UTF-8, trailing whitespace removed).
[[nodiscard]] std::string FormatSystemMessage(DWORD code);

// Maps well-known Win32 codes to portable categories (NotFound, AccessDenied, Timeout, ...).
[[nodiscard]] ErrorCode CategorizeWin32(DWORD code) noexcept;

[[nodiscard]] Error ErrorFromWin32(DWORD code, std::string_view context);
[[nodiscard]] Error ErrorFromHResult(HRESULT hr, std::string_view context);
// Captures ::GetLastError() immediately.
[[nodiscard]] Error LastWin32Error(std::string_view context);

}  // namespace aether::platform
