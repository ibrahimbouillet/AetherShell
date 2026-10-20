// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/MonitorUtilities.h"

#include "platform/windows/DpiUtilities.h"
#include "platform/windows/WindowUtilities.h"

namespace aether::platform {

std::optional<MonitorInfo> QueryMonitor(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!monitor || !::GetMonitorInfoW(monitor, &info)) return std::nullopt;
    MonitorInfo result;
    result.handle = monitor;
    result.bounds = info.rcMonitor;
    result.workArea = info.rcWork;
    result.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    result.deviceName = info.szDevice;
    result.dpi = DpiForMonitor(monitor);
    return result;
}

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<HMONITOR> handles;
    ::EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&handles));
    std::vector<MonitorInfo> monitors;
    for (HMONITOR handle : handles) {
        if (auto info = QueryMonitor(handle)) monitors.push_back(std::move(*info));
    }
    return monitors;
}

MonitorInfo PrimaryMonitor() {
    if (auto info = QueryMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY))) return *info;
    MonitorInfo fallback;
    fallback.bounds = RECT{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback.workArea, 0);
    fallback.primary = true;
    fallback.dpi = ::GetDpiForSystem();
    return fallback;
}

MonitorInfo MonitorForWindow(HWND hwnd) {
    if (auto info = QueryMonitor(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST))) return *info;
    return PrimaryMonitor();
}

bool IsFullscreenWindowGeometry(const RECT& windowBounds, const RECT& monitorBounds, DWORD style) noexcept {
    if (windowBounds.right <= windowBounds.left || windowBounds.bottom <= windowBounds.top ||
        monitorBounds.right <= monitorBounds.left || monitorBounds.bottom <= monitorBounds.top) return false;
    // Maximized framed windows can cover rcMonitor when the native taskbar is hidden/auto-hidden.
    // Fullscreen can still carry WS_MAXIMIZE and need not carry WS_POPUP, so neither is a discriminator.
    if ((style & (WS_CAPTION | WS_THICKFRAME | WS_CHILD)) != 0) return false;
    return windowBounds.left <= monitorBounds.left && windowBounds.top <= monitorBounds.top &&
           windowBounds.right >= monitorBounds.right && windowBounds.bottom >= monitorBounds.bottom;
}

bool IsFullscreenWindow(HWND window, const MonitorInfo& monitor) noexcept {
    if (!window || !::IsWindowVisible(window) || ::IsIconic(window)) return false;
    if (window == ::GetDesktopWindow() || window == ::GetShellWindow()) return false;
    const std::wstring className = WindowClassName(window);
    if (className == L"Progman" || className == L"WorkerW" || className == L"Shell_TrayWnd" ||
        className == L"Shell_SecondaryTrayWnd") {
        return false;
    }
    if (IsWindowCloaked(window)) return false;
    RECT rect{};
    if (!::GetWindowRect(window, &rect)) return false;
    ::SetLastError(ERROR_SUCCESS);
    const auto style = static_cast<DWORD>(::GetWindowLongPtrW(window, GWL_STYLE));
    if (style == 0 && ::GetLastError() != ERROR_SUCCESS) return false;
    return IsFullscreenWindowGeometry(rect, monitor.bounds, style);
}

}  // namespace aether::platform
