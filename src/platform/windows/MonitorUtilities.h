// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace aether::platform {

struct MonitorInfo {
    HMONITOR handle = nullptr;
    RECT bounds{};    // physical pixels, virtual-screen coordinates
    RECT workArea{};  // bounds minus taskbar and registered app bars
    UINT dpi = 96;
    bool primary = false;
    std::wstring deviceName;  // \\.\DISPLAY1
};

[[nodiscard]] std::vector<MonitorInfo> EnumerateMonitors();
[[nodiscard]] std::optional<MonitorInfo> QueryMonitor(HMONITOR monitor);
[[nodiscard]] MonitorInfo PrimaryMonitor();
[[nodiscard]] MonitorInfo MonitorForWindow(HWND hwnd);

[[nodiscard]] inline int Width(const RECT& r) noexcept { return r.right - r.left; }
[[nodiscard]] inline int Height(const RECT& r) noexcept { return r.bottom - r.top; }

// True when `window` is a visible, non-shell window that covers the whole monitor (borderless
// full-screen games, video players, presentations). The desktop and Explorer shell windows never count.
[[nodiscard]] bool IsFullscreenWindow(HWND window, const MonitorInfo& monitor) noexcept;

}  // namespace aether::platform
