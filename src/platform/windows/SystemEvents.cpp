// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/SystemEvents.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <wtsapi32.h>

#include <exception>

namespace aether::platform {
namespace {
constexpr wchar_t kClassName[] = L"AetherShell.SystemEvents";
}

SystemEvents::~SystemEvents() { Destroy(); }

Result<void> SystemEvents::Create() {
    if (hwnd_) return Ok();
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &SystemEvents::WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return LastWin32Error("RegisterClassEx(SystemEvents)");
    }
    taskbarCreatedMessage_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    // WS_EX_TOOLWINDOW keeps it out of Alt+Tab even if something ever showed it.
    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"Aether Shell system events", WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, instance, this);
    if (!hwnd_) return LastWin32Error("CreateWindowEx(SystemEvents)");
    // Allow TaskbarCreated through UIPI when Explorer runs at a different integrity level.
    if (taskbarCreatedMessage_) ::ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMessage_, MSGFLT_ALLOW, nullptr);
    sessionNotificationsRegistered_ = ::WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
    return Ok();
}

void SystemEvents::Destroy() noexcept {
    if (!hwnd_) return;
    if (sessionNotificationsRegistered_) {
        ::WTSUnRegisterSessionNotification(hwnd_);
        sessionNotificationsRegistered_ = false;
    }
    ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

LRESULT SystemEvents::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
            AETHER_LOG_INFO("system", "Explorer taskbar was (re)created");
            ExplorerRestarted.Emit();
            return 0;
        }
        switch (message) {
            case WM_TIMECHANGE: TimeChanged.Emit(); return 0;
            case WM_SETTINGCHANGE: {
                std::wstring section;
                if (lParam) {
                    const auto* text = reinterpret_cast<const wchar_t*>(lParam);
                    section.assign(text, ::wcsnlen(text, 256));
                }
                SettingChanged.Emit(static_cast<UINT>(wParam), section);
                return 0;
            }
            case WM_DISPLAYCHANGE: DisplayChanged.Emit(); return 0;
            case WM_THEMECHANGED:
            case WM_SYSCOLORCHANGE:
            case WM_DWMCOLORIZATIONCOLORCHANGED: ThemeChanged.Emit(); return 0;
            case WM_WTSSESSION_CHANGE: SessionChanged.Emit(wParam); return 0;
            case WM_POWERBROADCAST:
                if (wParam == PBT_APMRESUMEAUTOMATIC) ResumedFromSleep.Emit();
                if (wParam == PBT_POWERSETTINGCHANGE || wParam == PBT_APMPOWERSTATUSCHANGE) PowerSettingChanged.Emit();
                return TRUE;
            default: break;
        }
    } catch (const std::exception&) {
        AETHER_LOG_ERROR("system", "system event handler threw an exception");
    } catch (...) {
        AETHER_LOG_ERROR("system", "system event handler threw a non-standard exception");
    }
    return ::DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK SystemEvents::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<SystemEvents*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->hwnd_ == hwnd) return self->HandleMessage(message, wParam, lParam);
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace aether::platform
