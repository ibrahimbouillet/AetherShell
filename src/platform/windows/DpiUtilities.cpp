// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/DpiUtilities.h"

#include <shellscalingapi.h>

namespace aether::platform {

UINT DpiForWindow(HWND hwnd) noexcept {
    const UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : ::GetDpiForSystem();
}

UINT DpiForMonitor(HMONITOR monitor) noexcept {
    UINT dpiX = 0;
    UINT dpiY = 0;
    if (monitor && SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX) return dpiX;
    return ::GetDpiForSystem();
}

UINT DpiForPoint(POINT point) noexcept { return DpiForMonitor(::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST)); }

int SystemMetricForDpi(int index, UINT dpi) noexcept { return ::GetSystemMetricsForDpi(index, dpi ? dpi : kDefaultDpi); }

void EnsurePerMonitorDpiAwareness() noexcept {
    const auto current = ::GetThreadDpiAwarenessContext();
    if (::AreDpiAwarenessContextsEqual(current, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

}  // namespace aether::platform
