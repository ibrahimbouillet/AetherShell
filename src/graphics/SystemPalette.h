// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Reads the Windows accent palette and text scale through the documented WinRT UISettings API, and
// builds the design tokens that shell surfaces and app windows use. Call these on the UI thread
// once per theme or setting change, never per frame: the first UISettings call loads WinRT
// components. No exception leaves these functions; a failed read returns available=false and the
// brand palette applies.
#pragma once

#include "graphics/Theme.h"

namespace aether::graphics {

[[nodiscard]] SystemPalette QuerySystemPalette() noexcept;

// Tokens for the shell surfaces (Dock, Top Bar, Search, tooltips, menus): the Windows mode
// (DetectShellThemeMode) with the preference applied, plus QuerySystemPalette() and the accent source.
[[nodiscard]] DesignTokens MakeShellDesignTokens(const AppearanceOptions& options);
// Tokens for app windows (Settings): the app mode (DetectAppThemeMode) with the same overrides.
[[nodiscard]] DesignTokens MakeAppDesignTokens(const AppearanceOptions& options);

}  // namespace aether::graphics
