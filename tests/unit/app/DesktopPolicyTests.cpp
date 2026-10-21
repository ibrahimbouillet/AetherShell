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

AETHER_TEST("desktop policy: Dock remains visible until native taskbar restoration completes") {
    CHECK_FALSE(ShouldSuppressDock(false, Update(true, true, true)));
    CHECK(ShouldSuppressDock(false, Update(false, false, true)));
    CHECK_FALSE(ShouldSuppressDock(false, Update(false, true, false)));
    CHECK(ShouldSuppressDock(true, Update(true, true, true)));
}

AETHER_TEST("desktop policy: automatic pin import respects sync opt-out after first-use initialization") {
    CHECK_FALSE(ShouldAutomaticallyImportPins(false, true, false, false));
    CHECK_FALSE(ShouldAutomaticallyImportPins(true, false, true, true));
    CHECK(ShouldAutomaticallyImportPins(true, true, true, true));
    CHECK(ShouldAutomaticallyImportPins(true, false, false, true));
    CHECK(ShouldAutomaticallyImportPins(true, false, true, false));
}

AETHER_TEST("desktop policy: stale reveal timer cannot resume a canceled or newer reveal") {
    const auto suspended = Update(false, false, true);
    CHECK_FALSE(TaskbarRevealDelay(false, true, true, false, suspended, 100, 200).has_value());
    CHECK_FALSE(TaskbarRevealDelay(true, false, true, false, suspended, 100, 200).has_value());
    CHECK_FALSE(TaskbarRevealDelay(true, true, false, false, suspended, 100, 200).has_value());
    CHECK_FALSE(TaskbarRevealDelay(true, true, true, true, suspended, 100, 200).has_value());
    CHECK_FALSE(TaskbarRevealDelay(true, true, true, false, Update(true,true,true),100,200).has_value());
    CHECK_FALSE(TaskbarRevealDelay(true, true, true, false, Update(false,false),100,200).has_value());
    REQUIRE(TaskbarRevealDelay(true,true,true,false,suspended,100,200).has_value());
    CHECK_EQ(*TaskbarRevealDelay(true,true,true,false,suspended,100,200),std::uint64_t{100});
    CHECK_EQ(*TaskbarRevealDelay(true,true,true,false,suspended,200,200),std::uint64_t{0});
    CHECK_EQ(*TaskbarRevealDelay(true,true,true,false,suspended,201,200),std::uint64_t{0});
}

AETHER_TEST("desktop attention: bounded Dock peek cannot reveal native bar or override suspension") {
    CHECK_FALSE(ShouldSuppressDock(true,Update(true,false),true));
    CHECK(ShouldSuppressDock(true,Update(true,false),false));
    CHECK(ShouldSuppressDock(true,Update(false,false),true));
    CHECK(ShouldSuppressDock(true,Update(false,false,true),true));
    CHECK(ShouldSuppressDock(true,Update(true,true,true),true));
    CHECK_FALSE(ShouldSuppressDock(false,Update(true,false),false));
    CHECK_FALSE(AttentionPeekDelay(false,false,true,100,200));
    CHECK_FALSE(AttentionPeekDelay(true,true,true,100,200));
    CHECK_FALSE(AttentionPeekDelay(true,false,false,100,200));
    CHECK(AttentionPeekDelay(true,false,true,100,200)==100);
    CHECK(AttentionPeekDelay(true,false,true,200,200)==0);
    CHECK(AttentionPeekDelay(true,false,true,250,200)==0);
}
