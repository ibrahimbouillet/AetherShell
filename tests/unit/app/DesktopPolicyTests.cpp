// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Pure policy tables; nothing here reads or changes the Windows taskbar or the work area.
#include "AetherTest.h"
#include "app/DesktopPolicy.h"

using namespace aether;
using namespace aether::app;

namespace {
TaskbarUpdate Update(bool active, bool scheduled, bool suspended = false) {
    TaskbarUpdate update; update.active = active; update.scheduled = scheduled; update.suspended = suspended;
    return update;
}
}  // namespace

AETHER_TEST("desktop policy: only the installed instance may reach the native taskbar") {
    CHECK(MayAccessNativeTaskbar(L"AetherShell", true));
    CHECK_FALSE(MayAccessNativeTaskbar(L"AetherShell", false));
    CHECK_FALSE(MayAccessNativeTaskbar(L"AetherShell.Test.1234", true));
    CHECK_FALSE(MayAccessNativeTaskbar(L"aethershell", true));
    CHECK_FALSE(MayAccessNativeTaskbar(L"", true));
}

AETHER_TEST("desktop policy: the Dock keeps the replacement reservation only while it is needed") {
    // Activation requested or retrying.
    CHECK(ShouldForceDockReservation(false, true, Update(false, true)));
    // Active session, also while it is being restored after the setting was turned off.
    CHECK(ShouldForceDockReservation(false, true, Update(true, true)));
    CHECK(ShouldForceDockReservation(false, false, Update(true, true)));
    // The controller gave up: the Windows taskbar is back and reserves its own space.
    CHECK_FALSE(ShouldForceDockReservation(false, true, Update(false, false)));
    // Turned off and restored, or a restoration intermediate without a session.
    CHECK_FALSE(ShouldForceDockReservation(false, false, Update(false, false)));
    CHECK_FALSE(ShouldForceDockReservation(false, false, Update(false, true)));
    // Temporarily suspended: kept until the session is gone, then released.
    CHECK(ShouldForceDockReservation(false, true, Update(true, true, true)));
    CHECK_FALSE(ShouldForceDockReservation(false, true, Update(false, true, true)));
    CHECK_FALSE(ShouldForceDockReservation(false, true, Update(false, false, true)));
    // A failed reservation is never re-enabled by any update.
    for (bool active : {false, true}) for (bool scheduled : {false, true}) for (bool wanted : {false, true})
        CHECK_FALSE(ShouldForceDockReservation(true, wanted, Update(active, scheduled)));
}

AETHER_TEST("desktop policy: the poll timer stops once nothing more is scheduled") {
    CHECK(ShouldObserveTaskbar(Update(true, true)));
    CHECK(ShouldObserveTaskbar(Update(true, false)));  // a stuck restoration still has a session
    CHECK(ShouldObserveTaskbar(Update(false, true)));
    CHECK_FALSE(ShouldObserveTaskbar(Update(false, false)));
    CHECK_FALSE(ShouldObserveTaskbar(Update(false, false, true)));
}
