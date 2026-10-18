// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "core/Signal.h"

#include <windows.h>

#include <string>

namespace aether::platform {

// Receives system-wide broadcast messages through a hidden (never shown) top-level window and
// republishes them as signals on the UI thread. Message-only windows do not receive broadcasts,
// which is why a top-level window is used.
class SystemEvents {
public:
    SystemEvents() = default;
    ~SystemEvents();
    SystemEvents(const SystemEvents&) = delete;
    SystemEvents& operator=(const SystemEvents&) = delete;

    [[nodiscard]] Result<void> Create();  // UI thread
    void Destroy() noexcept;              // UI thread
    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }

    Signal<> TimeChanged;                        // WM_TIMECHANGE (clock or time zone changed)
    Signal<UINT, std::wstring> SettingChanged;   // WM_SETTINGCHANGE (SPI code, section e.g. "intl", "ImmersiveColorSet")
    Signal<> DisplayChanged;                     // WM_DISPLAYCHANGE (resolution / monitor layout)
    Signal<> ExplorerRestarted;                  // registered "TaskbarCreated" message
    Signal<> ThemeChanged;                       // WM_THEMECHANGED / WM_SYSCOLORCHANGE / WM_DWMCOLORIZATIONCOLORCHANGED
    Signal<> ResumedFromSleep;                   // WM_POWERBROADCAST / PBT_APMRESUMEAUTOMATIC
    Signal<WPARAM> SessionChanged;               // WM_WTSSESSION_CHANGE (lock, unlock, remote connect...)
    Signal<> PowerSettingChanged;                // WM_POWERBROADCAST / PBT_POWERSETTINGCHANGE / PBT_APMPOWERSTATUSCHANGE

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    bool sessionNotificationsRegistered_ = false;
};

}  // namespace aether::platform
