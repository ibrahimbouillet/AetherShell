// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The process is Per-Monitor-V2 DPI aware (see the application manifest).
// Layout is expressed in device-independent pixels (DIPs, 1/96 inch) and
// converted with the DPI of the monitor that hosts each window.
#pragma once

#include <windows.h>

#include <cmath>

namespace aether::platform {

inline constexpr UINT kDefaultDpi = USER_DEFAULT_SCREEN_DPI;  // 96

[[nodiscard]] inline float ScaleForDpi(UINT dpi) noexcept { return static_cast<float>(dpi ? dpi : kDefaultDpi) / 96.0f; }
[[nodiscard]] inline int DipsToPixels(float dips, UINT dpi) noexcept {
    return static_cast<int>(std::lround(dips * ScaleForDpi(dpi)));
}
[[nodiscard]] inline float PixelsToDips(int pixels, UINT dpi) noexcept {
    return static_cast<float>(pixels) / ScaleForDpi(dpi);
}

[[nodiscard]] UINT DpiForWindow(HWND hwnd) noexcept;
[[nodiscard]] UINT DpiForMonitor(HMONITOR monitor) noexcept;
[[nodiscard]] UINT DpiForPoint(POINT point) noexcept;

// System metrics scaled for a DPI (GetSystemMetricsForDpi).
[[nodiscard]] int SystemMetricForDpi(int index, UINT dpi) noexcept;

// Makes the calling process Per-Monitor-V2 aware if the manifest did not (tests, tools).
void EnsurePerMonitorDpiAwareness() noexcept;

}  // namespace aether::platform
