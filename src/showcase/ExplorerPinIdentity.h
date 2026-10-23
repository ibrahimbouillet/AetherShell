// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once
#include "core/Result.h"
#include <string>
#include <string_view>
namespace aether::showcase {
// Reads only the localized presentation name of the single genuine Windows Explorer pin.
// Settings and paths remain runtime-only; errors never echo them. No application is launched.
[[nodiscard]] Result<std::wstring> ResolveExplorerPinName(std::string_view settingsJson,
    std::wstring_view systemExplorerExecutable) noexcept;
}
