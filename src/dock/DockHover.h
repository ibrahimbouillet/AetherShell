// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure hover and window-preview state of the Dock (FR-033/FR-034, T42/T43). It decides which running
// application's marker is emphasized and whose preview card is shown; the window owns the timers,
// HWNDs and rendering and calls Advance() at Deadline(). Nothing here activates or closes a window:
// emphasis is presentation only and never changes the observed foreground state.
//
// An icon and its card form one hover interaction. Leaving the icon while a card is shown starts a
// short crossing grace; entering the card (or returning to the icon) keeps both the card and the
// emphasis. Leaving both for longer than the grace dismisses the card and contracts the marker.
// Application ids compare case-insensitively.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace aether::dock {

// Seconds; the window scales them with the motion policy where appropriate. The preview dwell is
// deliberately longer than the name tooltip's delay so passing over icons never opens cards.
struct DockHoverTiming {
    double dwell = 0.6;          // pointer rests on a running icon before its card appears
    double warmSwitch = 0.12;    // card already shown: moving to another running icon switches after this
    double grace = 0.35;         // crossing between an icon and its card (and back) without dismissal
    double activationHold = 0.8; // emphasis kept after a click so the observed activation continues it
};

class DockHoverSession {
public:
    explicit DockHoverSession(DockHoverTiming timing = {}) noexcept : timing_(timing) {}
    void SetTiming(const DockHoverTiming& timing) noexcept { timing_ = timing; }
    [[nodiscard]] const DockHoverTiming& Timing() const noexcept { return timing_; }

    // The pointer is over the Dock window. `id` names the application item under it; empty means
    // the panel or a non-application entry. `running`: an observed running group (marker exists).
    // `previewable`: it has at least one live window that a card could show.
    void PointerOnItem(std::wstring_view id, bool running, bool previewable, double now);
    void PointerOffDock(double now);
    void PointerOnPreview(double now);
    void PointerOffPreview(double now);
    // Keyboard request for the focused item's card: no dwell and no pointer grace.
    void OpenForKeyboard(std::wstring_view id);
    void Advance(double now);
    // The application was activated from its icon or card. The card closes and stays closed until the
    // pointer leaves that icon; the marker stays emphasized for activationHold so a genuine
    // foreground transfer continues the already expanded width instead of contracting first.
    void Activated(std::wstring_view id, double now);
    void ActivationFailed() noexcept;
    // The card cannot show this application any longer (no live window, policy, failure).
    void PreviewUnavailable(std::wstring_view id);
    void Reset() noexcept;

    [[nodiscard]] const std::wstring& Previewed() const noexcept { return previewed_; }
    [[nodiscard]] const std::wstring& Emphasized() const noexcept { return emphasized_; }
    // Emphasized() or within the post-activation hold.
    [[nodiscard]] bool Emphasizes(std::wstring_view id) const noexcept;
    [[nodiscard]] bool KeyboardPreview() const noexcept { return keyboard_; }
    [[nodiscard]] bool PointerOnPreviewSurface() const noexcept { return onPreview_; }
    [[nodiscard]] const std::wstring& ItemUnderPointer() const noexcept { return item_; }
    [[nodiscard]] std::optional<double> Deadline() const noexcept;

private:
    void LeaveItem(double now);
    void StartGrace(double now);
    void Recompute();

    DockHoverTiming timing_;
    std::wstring item_;
    bool itemRunning_ = false;
    bool itemPreviewable_ = false;
    bool onPreview_ = false;
    bool keyboard_ = false;
    std::wstring previewed_;
    std::wstring emphasized_;
    std::wstring suppressed_;
    std::wstring dwellTarget_;
    std::optional<double> dwellDeadline_;
    std::optional<double> graceDeadline_;
    std::wstring holdId_;
    std::optional<double> holdDeadline_;
};

}  // namespace aether::dock
