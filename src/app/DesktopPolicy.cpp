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

}  // namespace aether::app
