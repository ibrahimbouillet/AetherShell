// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "welcome/WelcomeController.h"

namespace aether::welcome {

WelcomeController::Action WelcomeController::Begin(std::uint64_t now) noexcept {
    presenting_ = true;
    hasStarted_ = true;
    startedAt_ = now;
    return Action::Present;
}

WelcomeController::Action WelcomeController::SignedIn(bool allowed, std::uint64_t now) noexcept {
    if (signInHandled_) return Action::None;
    signInHandled_ = true;
    // Started behind a locked screen: the unlock that follows is the greeting.
    if (!allowed || presenting_ || locked_) return Action::None;
    return Begin(now);
}

WelcomeController::Action WelcomeController::Unlocked(bool allowed, std::uint64_t now) noexcept {
    locked_ = false;
    const bool covered = covering_;
    covering_ = false;
    if (!allowed) return covered ? Action::DismissNow : Action::None;
    if (presenting_) return Action::None;
    // Windows can repeat session notifications; a greeting that just ran is the same unlock.
    if (hasStarted_ && now - startedAt_ < timing_.duplicateWindowMs) return Action::None;
    return Begin(now);
}

WelcomeController::Action WelcomeController::Locked(bool allowed, std::uint64_t) noexcept {
    locked_ = true;
    hasStarted_ = false;  // the next unlock is a new event, however soon it comes
    const bool shown = presenting_ || covering_;
    presenting_ = false;
    if (allowed) {
        if (covering_) return Action::None;
        covering_ = true;
        return Action::Cover;
    }
    covering_ = false;
    return shown ? Action::DismissNow : Action::None;
}

WelcomeController::Action WelcomeController::Canceled() noexcept {
    if (!presenting_ && !covering_) return Action::None;
    presenting_ = covering_ = false;
    return Action::DismissNow;
}

WelcomeController::Action WelcomeController::Skipped() noexcept {
    if (covering_) {
        covering_ = false;
        return Action::DismissNow;
    }
    if (!presenting_) return Action::None;
    presenting_ = false;
    return Action::DismissAnimated;
}

WelcomeController::Action WelcomeController::Update(bool writingFinished, bool essentialReady, std::uint64_t now) noexcept {
    if (!presenting_) return Action::None;
    if (now - startedAt_ >= timing_.deadlineMs || (writingFinished && essentialReady)) {
        presenting_ = false;
        return Action::DismissAnimated;
    }
    return Action::None;
}

}  // namespace aether::welcome
