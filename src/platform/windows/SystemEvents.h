// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "core/Signal.h"

#include <windows.h>

#include <functional>
#include <string>

namespace aether::platform {

// Receives system-wide broadcast messages through a hidden (never shown) top-level window and
// republishes them as signals on the UI thread. Message-only windows do not receive broadcasts,
// which is why a top-level window is used.
// AS4-22: session (lock/unlock/logon) notifications. Registration can fail early in a sign-in,
// before the session service is ready; it is retried with a bounded back-off, and an explicit
// Unavailable state is reported once the budget is spent. Never silently "registered".
enum class SessionNotifications { Pending, Registered, Unavailable };

// What Windows says about this session now, independently of any notification (FR-041 uses it so a
// missed notification can never leave its cover up).
enum class SessionLockState { Locked, Unlocked, Unknown };
[[nodiscard]] SessionLockState CurrentSessionLockState() noexcept;

struct SessionRegistrar {
    std::function<bool(HWND)> registerWindow;    // WTSRegisterSessionNotification(NOTIFY_FOR_THIS_SESSION)
    std::function<void(HWND)> unregisterWindow;  // paired with every successful registration
    UINT firstRetryMs = 1000;                    // doubled after every failure, capped at 30 s
    unsigned maximumAttempts = 8;
};

class SystemEvents {
public:
    SystemEvents() = default;
    // Tests inject the registration seam; production uses the WTS functions.
    explicit SystemEvents(SessionRegistrar registrar) : registrar_(std::move(registrar)) {}
    ~SystemEvents();
    SystemEvents(const SystemEvents&) = delete;
    SystemEvents& operator=(const SystemEvents&) = delete;

    [[nodiscard]] Result<void> Create();  // UI thread
    void Destroy() noexcept;              // UI thread
    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }
    [[nodiscard]] SessionNotifications SessionNotificationState() const noexcept { return sessionState_; }
    [[nodiscard]] unsigned SessionRegistrationAttempts() const noexcept { return sessionAttempts_; }

    Signal<> TimeChanged;                        // WM_TIMECHANGE (clock or time zone changed)
    Signal<UINT, std::wstring> SettingChanged;   // WM_SETTINGCHANGE (SPI code, section e.g. "intl", "ImmersiveColorSet")
    Signal<> DisplayChanged;                     // WM_DISPLAYCHANGE (resolution / monitor layout)
    Signal<> ExplorerRestarted;                  // registered "TaskbarCreated" message
    Signal<> ThemeChanged;                       // WM_THEMECHANGED / WM_SYSCOLORCHANGE / WM_DWMCOLORIZATIONCOLORCHANGED
    Signal<> ResumedFromSleep;                   // WM_POWERBROADCAST / PBT_APMRESUMEAUTOMATIC
    Signal<WPARAM> SessionChanged;               // WM_WTSSESSION_CHANGE (lock, unlock, remote connect...)
    Signal<SessionNotifications> SessionNotificationStateChanged;  // registered late, or given up
    Signal<LPARAM> RawInput;                     // WM_INPUT, for a raw-input registration on this window
    Signal<> PowerSettingChanged;                // WM_POWERBROADCAST / PBT_POWERSETTINGCHANGE / PBT_APMPOWERSTATUSCHANGE

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void TryRegisterSession() noexcept;

    SessionRegistrar registrar_;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    bool sessionNotificationsRegistered_ = false;
    SessionNotifications sessionState_ = SessionNotifications::Pending;
    unsigned sessionAttempts_ = 0;
};

}  // namespace aether::platform
