// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/SystemEvents.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <wtsapi32.h>

#include <algorithm>
#include <exception>

namespace aether::platform {
namespace {
constexpr wchar_t kClassName[] = L"AetherShell.SystemEvents";
constexpr UINT_PTR kSessionRetryTimer = 1;
}

SessionLockState CurrentSessionLockState() noexcept {
    LPWSTR buffer = nullptr;
    DWORD size = 0;
    if (!::WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &buffer, &size) || !buffer) return SessionLockState::Unknown;
    auto state = SessionLockState::Unknown;
    if (size >= sizeof(WTSINFOEXW)) {
        const auto* info = reinterpret_cast<const WTSINFOEXW*>(buffer);
        if (info->Level == 1) {
            if (info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK) state = SessionLockState::Locked;
            else if (info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK) state = SessionLockState::Unlocked;
        }
    }
    ::WTSFreeMemory(buffer);
    return state;
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
    if (!registrar_.registerWindow) {
        registrar_.registerWindow = [](HWND window) { return ::WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION) != FALSE; };
        registrar_.unregisterWindow = [](HWND window) { (void)::WTSUnRegisterSessionNotification(window); };
    }
    sessionAttempts_ = 0;
    sessionState_ = SessionNotifications::Pending;
    TryRegisterSession();
    return Ok();
}

// One attempt now; on failure one timer for the next. The timer belongs to this window, so
// Destroy (shutdown during a retry) cancels it with the window.
void SystemEvents::TryRegisterSession() noexcept {
    if (!hwnd_ || sessionNotificationsRegistered_ || sessionState_ == SessionNotifications::Unavailable) return;
    ++sessionAttempts_;
    bool registered = false;
    try { registered = registrar_.registerWindow && registrar_.registerWindow(hwnd_); } catch (...) {}
    if (registered) {
        ::KillTimer(hwnd_, kSessionRetryTimer);
        sessionNotificationsRegistered_ = true;
        const bool late = sessionAttempts_ > 1;
        sessionState_ = SessionNotifications::Registered;
        if (late) {
            AETHER_LOG_INFO("system", "session notifications registered after {} attempts", sessionAttempts_);
            try { SessionNotificationStateChanged.Emit(sessionState_); } catch (...) {}
        }
        return;
    }
    if (sessionAttempts_ >= std::max(1u, registrar_.maximumAttempts)) {
        ::KillTimer(hwnd_, kSessionRetryTimer);
        sessionState_ = SessionNotifications::Unavailable;
        AETHER_LOG_WARN("system", "session notifications unavailable after {} attempts", sessionAttempts_);
        try { SessionNotificationStateChanged.Emit(sessionState_); } catch (...) {}
        return;
    }
    const auto shift = std::min(sessionAttempts_ - 1, 5u);
    const UINT delay = std::min<UINT>(30000, std::max<UINT>(1, registrar_.firstRetryMs) << shift);
    if (!::SetTimer(hwnd_, kSessionRetryTimer, delay, nullptr)) {
        sessionState_ = SessionNotifications::Unavailable;
        AETHER_LOG_WARN("system", "session notification retry could not be scheduled");
        try { SessionNotificationStateChanged.Emit(sessionState_); } catch (...) {}
    }
}

void SystemEvents::Destroy() noexcept {
    if (!hwnd_) return;
    ::KillTimer(hwnd_, kSessionRetryTimer);
    if (sessionNotificationsRegistered_) {
        try { if (registrar_.unregisterWindow) registrar_.unregisterWindow(hwnd_); } catch (...) {}
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
            case WM_INPUT: RawInput.Emit(lParam); break;  // DefWindowProc still completes the input
            case WM_TIMER:
                if (wParam == kSessionRetryTimer) { ::KillTimer(hwnd_, kSessionRetryTimer); TryRegisterSession(); return 0; }
                break;
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
