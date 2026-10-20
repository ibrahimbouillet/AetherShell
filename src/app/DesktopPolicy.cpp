// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/DesktopPolicy.h"

namespace aether::app {

bool MayAccessNativeTaskbar(std::wstring_view instanceName, bool requested) noexcept {
    return requested && instanceName == kProductionInstanceName;
}

bool ShouldForceDockReservation(bool reservationFailed, bool replacementWanted,
                                const TaskbarUpdate& update) noexcept {
    if (reservationFailed) return false;
    return update.active || (replacementWanted && !update.suspended && update.scheduled);
}

bool ShouldObserveTaskbar(const TaskbarUpdate& update) noexcept {
    return update.active || update.scheduled;
}

bool ShouldSuppressDock(bool fullscreen, const TaskbarUpdate& update) noexcept {
    return fullscreen || (update.suspended && !update.active);
}

bool ShouldAutomaticallyImportPins(bool replacement, bool sync, bool imported, bool explorerApplied) noexcept {
    return replacement && (sync || !imported || !explorerApplied);
}

std::optional<std::uint64_t> TaskbarRevealDelay(bool automatic, bool armed, bool replacement,
    bool closing, const TaskbarUpdate& update, std::uint64_t now, std::uint64_t deadline) noexcept {
    if (!automatic || !armed || !replacement || closing || !update.suspended || update.active)
        return std::nullopt;
    return now < deadline ? deadline - now : 0;
}

}  // namespace aether::app
