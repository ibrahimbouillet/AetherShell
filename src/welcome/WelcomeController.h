// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-041/ADR 0013 lifecycle, pure and unit tested: one greeting per sign-in and per unlock, on the
// normal desktop only. D54: when the session locks, the greeting's background goes up behind Windows'
// lock screen, so the unlock reveals it rather than the desktop; the writing starts at the unlock.
// Duplicate notifications coalesce, a re-lock or disconnect stops the writing at once, a skip ends it,
// and it ends by itself once the writing is finished and the essential surfaces have really presented.
// Whatever else happens, an independent deadline ends it: nothing can keep the desktop covered, optional
// providers included (they are simply not part of readiness).
#pragma once

#include <cstdint>

namespace aether::welcome {

struct WelcomeTiming {
    std::uint64_t deadlineMs = 6000;         // the hard limit on any presentation
    std::uint64_t duplicateWindowMs = 3000;  // a repeat notification this soon is the same event
};

class WelcomeController {
public:
    enum class Action { None, Cover, Present, DismissAnimated, DismissNow };

    explicit WelcomeController(WelcomeTiming timing = {}) noexcept : timing_(timing) {}

    // `allowed`: the person's setting, and no reason to stay out of the way (closing, full screen).
    [[nodiscard]] Action SignedIn(bool allowed, std::uint64_t now) noexcept;
    [[nodiscard]] Action Unlocked(bool allowed, std::uint64_t now) noexcept;
    // Locked, disconnected or logging off. `allowed` asks for the cover; only a real lock does.
    [[nodiscard]] Action Locked(bool allowed, std::uint64_t now) noexcept;
    [[nodiscard]] Action Canceled() noexcept;                  // display change, quitting: not a lock
    [[nodiscard]] Action Skipped() noexcept;                   // touch, mouse or Escape
    [[nodiscard]] Action Update(bool writingFinished, bool essentialReady, std::uint64_t now) noexcept;
    void PresentationFailed() noexcept { presenting_ = covering_ = false; }  // a graphics failure is never an obstacle

    [[nodiscard]] bool Presenting() const noexcept { return presenting_; }
    [[nodiscard]] bool Covering() const noexcept { return covering_; }
    [[nodiscard]] bool SessionLocked() const noexcept { return locked_; }

private:
    [[nodiscard]] Action Begin(std::uint64_t now) noexcept;

    WelcomeTiming timing_;
    bool presenting_ = false;
    bool covering_ = false;
    bool locked_ = false;
    bool signInHandled_ = false;
    bool hasStarted_ = false;
    std::uint64_t startedAt_ = 0;
};

}  // namespace aether::welcome
