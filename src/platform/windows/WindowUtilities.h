// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <string>

namespace aether::platform {

[[nodiscard]] std::wstring WindowClassName(HWND hwnd);
[[nodiscard]] std::wstring WindowTitle(HWND hwnd);

// DWM cloaking hides windows that are "visible" to USER32 (UWP frames on other virtual desktops,
// suspended packaged apps). Cloaked windows must not be treated as user-visible.
[[nodiscard]] bool IsWindowCloaked(HWND hwnd) noexcept;

// Uses the dark title bar / frame for windows that have one (settings, update dialog).
void ApplyImmersiveDarkMode(HWND hwnd, bool dark) noexcept;

// Rounded-corner preference for top-level windows on Windows 11 (ignored on Windows 10).
enum class CornerPreference { Default = 0, DoNotRound = 1, Round = 2, RoundSmall = 3 };
void SetCornerPreference(HWND hwnd, CornerPreference preference) noexcept;

// Excludes a window from Windows' own taskbar/Alt+Tab peek and live preview (DWMWA_EXCLUDED_FROM_PEEK).
void ExcludeFromPeek(HWND hwnd) noexcept;

// System colour mode: true when apps should use the dark theme (UISettings-equivalent registry value
// AppsUseLightTheme under Personalize; defaults to light when unavailable).
[[nodiscard]] bool SystemPrefersDarkApps() noexcept;
// Windows (taskbar and Start) colour mode: true when the shell should use the dark theme. Reads the
// undocumented SystemUsesLightTheme value of the same Personalize key (cosmetic only); when it is absent,
// falls back to SystemPrefersDarkApps().
[[nodiscard]] bool SystemPrefersDarkShell() noexcept;
// "Transparency effects" setting (EnableTransparency); defaults to enabled.
[[nodiscard]] bool SystemTransparencyEnabled() noexcept;
// High-contrast theme active.
[[nodiscard]] bool HighContrastEnabled() noexcept;
// "Show animations in Windows" (SPI_GETCLIENTAREAANIMATION); defaults to enabled.
[[nodiscard]] bool SystemAnimationsEnabled() noexcept;

// Registers a window class once per process; returns the atom or an error.
[[nodiscard]] Result<ATOM> RegisterWindowClassOnce(const WNDCLASSEXW& windowClass);

}  // namespace aether::platform
