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

// Pure geometry/style policy. A fullscreen window covers a valid monitor without a caption or
// resizable frame. WS_MAXIMIZE is allowed: borderless fullscreen can retain maximized state.
// All coordinates must use the same pixel space; no HWND or desktop state is inspected here.
[[nodiscard]] bool IsFullscreenWindowGeometry(const RECT& windowBounds, const RECT& monitorBounds, DWORD style) noexcept;

// True when `window` is a visible, non-cloaked, non-shell borderless window covering the monitor.
// Ordinary framed maximized applications do not count, even when auto-hide gives them full bounds.
[[nodiscard]] bool IsFullscreenWindow(HWND window, const MonitorInfo& monitor) noexcept;

}  // namespace aether::platform
