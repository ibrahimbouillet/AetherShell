// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/PanelDismissal.h"

#include "diagnostics/Logger.h"

#include <utility>

namespace aether::app {
namespace {

constexpr wchar_t kWindowClass[] = L"AetherShell.PanelDismissal";
constexpr int kEscapeHotkey = 1;
constexpr UINT_PTR kContactTimer = 1;
constexpr UINT kContactDecisionMs = 120;    // a contact on the panel has reached it well before this
constexpr std::uint64_t kContactSlackMs = 60;  // tick resolution, and the panel may hear of it first

// The WinEvent callback carries no context; one watcher exists per UI thread.
thread_local PanelDismissal* t_watching = nullptr;

}  // namespace

bool IsRawMousePress(USHORT buttonFlags) noexcept {
    return (buttonFlags & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN)) != 0;
}

PanelDismissal::~PanelDismissal() {
    Disarm();
    if (window_) ::DestroyWindow(window_);
}

bool PanelDismissal::EnsureWindow() noexcept {
    if (window_) return true;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &PanelDismissal::WindowProc;
    windowClass.hInstance = ::GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&windowClass) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window_ = ::CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, this);
    return window_ != nullptr;
}

void PanelDismissal::Arm(Callbacks callbacks) noexcept {
    if (armed_ || !EnsureWindow()) return;
    try { callbacks_ = std::move(callbacks); } catch (...) { return; }
    armed_ = true;
    contactAt_ = 0;
    RAWINPUTDEVICE mouse{};
    mouse.usUsagePage = 0x01;
    mouse.usUsage = 0x02;
    mouse.dwFlags = RIDEV_INPUTSINK;  // observes in the background; consumes nothing
    mouse.hwndTarget = window_;
    mouseRegistered_ = ::RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) != FALSE;
    if (!mouseRegistered_) AETHER_LOG_WARN("app", "a panel cannot watch mouse presses ({})", ::GetLastError());
    hotkeyRegistered_ = ::RegisterHotKey(window_, kEscapeHotkey, MOD_NOREPEAT, VK_ESCAPE) != FALSE;
    t_watching = this;
    foregroundHook_ = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &PanelDismissal::ForegroundChanged, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void PanelDismissal::Disarm() noexcept {
    if (!armed_) return;
    armed_ = false;
    if (foregroundHook_) ::UnhookWinEvent(foregroundHook_);
    foregroundHook_ = nullptr;
    if (t_watching == this) t_watching = nullptr;
    if (hotkeyRegistered_) ::UnregisterHotKey(window_, kEscapeHotkey);
    hotkeyRegistered_ = false;
    if (mouseRegistered_) {
        RAWINPUTDEVICE mouse{};
        mouse.usUsagePage = 0x01;
        mouse.usUsage = 0x02;
        mouse.dwFlags = RIDEV_REMOVE;
        (void)::RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
    }
    mouseRegistered_ = false;
    if (window_) ::KillTimer(window_, kContactTimer);
    contactAt_ = 0;
}

void PanelDismissal::ContactLanded() noexcept {
    if (!armed_ || contactAt_ != 0) return;  // the first contact decides; a second finger is the same gesture
    contactAt_ = ::GetTickCount64();
    (void)::SetTimer(window_, kContactTimer, kContactDecisionMs, nullptr);
}

void PanelDismissal::Dismiss(Reason reason) noexcept {
    if (!armed_ || !callbacks_.dismiss) return;
    try {
        const auto dismiss = callbacks_.dismiss;  // dismissing disarms, which replaces nothing but may run again
        dismiss(reason);
    } catch (...) {
        AETHER_LOG_ERROR("app", "dismissing a panel threw an exception");
    }
}

void PanelDismissal::OnRawInput(LPARAM input) noexcept {
    if (!armed_) return;
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(input), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    if (raw.header.dwType != RIM_TYPEMOUSE || !IsRawMousePress(raw.data.mouse.usButtonFlags)) return;
    POINT cursor{};
    if (!::GetCursorPos(&cursor)) return;
    try {
        if (callbacks_.insidePanel && callbacks_.insidePanel(cursor)) return;
    } catch (...) {
        AETHER_LOG_ERROR("app", "checking a press against a panel threw an exception");
        return;
    }
    Dismiss(Reason::Press);
}

void CALLBACK PanelDismissal::ForegroundChanged(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    // Out of context, delivered through this thread's message loop; another process took the foreground.
    if (event == EVENT_SYSTEM_FOREGROUND && t_watching) t_watching->Dismiss(Reason::Foreground);
}

LRESULT CALLBACK PanelDismissal::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<PanelDismissal*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self) {
        switch (message) {
            case WM_INPUT:
                self->OnRawInput(lParam);
                break;  // the default procedure cleans up after foreground input
            case WM_HOTKEY:
                if (wParam == kEscapeHotkey) self->Dismiss(Reason::Escape);
                return 0;
            case WM_TIMER:
                if (wParam == kContactTimer) {
                    ::KillTimer(window, kContactTimer);
                    const auto since = self->contactAt_;
                    self->contactAt_ = 0;
                    bool inside = false;
                    try {
                        inside = self->callbacks_.panelTouchedSince && self->callbacks_.panelTouchedSince(since > kContactSlackMs ? since - kContactSlackMs : 0);
                    } catch (...) {
                        AETHER_LOG_ERROR("app", "checking a touch against a panel threw an exception");
                        return 0;
                    }
                    if (self->armed_ && !inside) self->Dismiss(Reason::Touch);
                }
                return 0;
            default:
                break;
        }
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace aether::app
