// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D60: a panel closes as soon as anything else is used, and on Escape. Activation alone does not do it: a
// panel Windows did not give the foreground never hears of a click on the application that still has it,
// which is how Quick Settings stayed open over the page being used. So while a panel is open this watches:
//  * mouse presses anywhere, through raw input (no hook: the pointer is never slowed, and nothing is consumed);
//  * new touch contacts, reported by the host, which owns the digitizer's registration;
//  * the foreground moving to another process;
//  * Escape, through a hotkey that only exists while a panel is open, so it reaches the panel without its
//    needing the focus. No other key is seen.
// Positions are used only for this decision, never stored or logged.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

namespace aether::app {

// Whether a raw mouse report's button flags start a press of any button (pure, unit tested).
[[nodiscard]] bool IsRawMousePress(USHORT buttonFlags) noexcept;

// A press that closed a panel usually lands on the very control that toggles it; the click it ends must not
// open that panel again. The toggle asks here, once. A later, deliberate click opens it as usual.
class PressedAway {
public:
    void Mark(std::uint64_t now) noexcept { at_ = now; }
    [[nodiscard]] bool Take(std::uint64_t now) noexcept {
        const bool recent = at_ != 0 && now - at_ < kWindowMs;
        at_ = 0;
        return recent;
    }

private:
    static constexpr std::uint64_t kWindowMs = 500;
    std::uint64_t at_ = 0;
};

class PanelDismissal {
public:
    enum class Reason { Press, Touch, Foreground, Escape };
    struct Callbacks {
        std::function<bool(POINT)> insidePanel;                      // a screen point is on an open panel
        std::function<bool(std::uint64_t)> panelTouchedSince;        // an open panel received a contact since this tick
        std::function<void(Reason)> dismiss;
    };

    PanelDismissal() = default;
    ~PanelDismissal();
    PanelDismissal(const PanelDismissal&) = delete;
    PanelDismissal& operator=(const PanelDismissal&) = delete;

    // UI thread. Best effort: whatever Windows refuses, the panel still closes on deactivation.
    void Arm(Callbacks callbacks) noexcept;
    void Disarm() noexcept;
    [[nodiscard]] bool Armed() const noexcept { return armed_; }
    // The digitizer reported a new contact, somewhere. A touch on the panel reaches the panel, so it is known
    // to have been elsewhere once a moment has passed without that.
    void ContactLanded() noexcept;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK ForegroundChanged(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD);
    [[nodiscard]] bool EnsureWindow() noexcept;
    void Dismiss(Reason reason) noexcept;
    void OnRawInput(LPARAM input) noexcept;

    HWND window_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    Callbacks callbacks_;
    std::uint64_t contactAt_ = 0;
    bool armed_ = false;
    bool mouseRegistered_ = false;
    bool hotkeyRegistered_ = false;
};

}  // namespace aether::app
