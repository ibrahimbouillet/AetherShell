// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/AutoHide.h"

#include "platform/windows/WindowUtilities.h"

#include <dwmapi.h>

#include <array>
#include <string>
#include <string_view>

namespace aether::app {

bool AutoHideState::Update(const AutoHideSample& sample) noexcept {
    if (!sample.atEdge) edgeSince_.reset();
    else if (!edgeSince_) edgeSince_ = sample.now;
    if (!sample.enabled || !sample.covered) {
        // Off, or the desktop shows behind the bar: it stays, and a later cover starts afresh.
        revealed_ = false;
        return hidden_ = false;
    }
    const bool edgeReveal = edgeSince_ && sample.now - *edgeSince_ >= timing_.revealDwellMs;
    if (sample.engaged || edgeReveal) {
        revealed_ = true;
        lastUse_ = sample.now;
        return hidden_ = false;
    }
    if (revealed_ && sample.now - lastUse_ < timing_.hideDelayMs) return hidden_ = false;
    revealed_ = false;
    return hidden_ = true;
}

namespace {

// The desktop, the shell's taskbars and Explorer's own flyouts never count as covering a bar.
constexpr std::array<std::wstring_view, 8> kShellClasses{
    L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"TopLevelWindowForOverflowXamlIsland",
    L"NotifyIconOverflowWindow", L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow"};

}  // namespace

bool ApplicationWindowCovers(const RECT& zone) noexcept {
    if (zone.right <= zone.left || zone.bottom <= zone.top) return false;
    struct Search { RECT zone; DWORD self; bool found = false; } search{zone, ::GetCurrentProcessId()};
    ::EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(parameter);
        if (!::IsWindowVisible(window) || ::IsIconic(window)) return TRUE;
        DWORD process = 0;
        if (!::GetWindowThreadProcessId(window, &process)) return TRUE;
        const auto extended = static_cast<DWORD>(::GetWindowLongPtrW(window, GWL_EXSTYLE));
        if (extended & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) return TRUE;
        if (platform::IsWindowCloaked(window)) return TRUE;
        try {
            const auto name = platform::WindowClassName(window);
            for (const auto shell : kShellClasses)
                if (name == shell) return TRUE;
            // Of Aether's own windows only Settings is an application window; the bars, panels,
            // menus and infrastructure windows never cover anything.
            if (process == state.self && name != L"AetherShell.Settings") return TRUE;
        } catch (...) {
            return TRUE;
        }
        // The visible frame, without the invisible resize borders of ordinary windows.
        RECT bounds{};
        if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) && !::GetWindowRect(window, &bounds))
            return TRUE;
        RECT overlap{};
        if (!::IntersectRect(&overlap, &bounds, &state.zone)) return TRUE;
        state.found = true;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

}  // namespace aether::app
