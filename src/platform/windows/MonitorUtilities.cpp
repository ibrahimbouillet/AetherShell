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
    return rect.left <= monitor.bounds.left && rect.top <= monitor.bounds.top && rect.right >= monitor.bounds.right &&
           rect.bottom >= monitor.bounds.bottom;
}

}  // namespace aether::platform
