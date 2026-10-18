// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "status/WinRtSupport.h"

#include <objbase.h>
#include <thread>

using namespace aether;
using namespace aether::status;

AETHER_TEST("status runtime: final shutdown refuses live scopes and releases the MTA lease once") {
    HRESULT before = E_PENDING;
    HRESULT pinned = E_PENDING;
    HRESULT after = E_PENDING;
    bool initialized = false;
    bool refusedActive = false;
    bool finalized = false;
    bool terminal = false;
    std::jthread worker([&] {
        APTTYPE type{};
        APTTYPEQUALIFIER qualifier{};
        before = ::CoGetApartmentType(&type, &qualifier);
        {
            WinRtApartmentScope scope;
            initialized = scope.IsUsable();
            const auto stopped = FinalizeStatusRuntime();
            refusedActive = !stopped && stopped.error().code == ErrorCode::InvalidState;
        }
        pinned = ::CoGetApartmentType(&type, &qualifier);
        finalized = FinalizeStatusRuntime().ok() && FinalizeStatusRuntime().ok();
        after = ::CoGetApartmentType(&type, &qualifier);
        WinRtApartmentScope closed;
        terminal = !closed.IsUsable();
    });
    worker.join();
    CHECK_EQ(before, CO_E_NOTINITIALIZED);
    CHECK(initialized);
    CHECK(refusedActive);
    CHECK(SUCCEEDED(pinned));
    CHECK(finalized);
    CHECK_EQ(after, CO_E_NOTINITIALIZED);
    CHECK(terminal);
}
