// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/Win32CleanSessionBackend.h"

#include <Windows.h>

#include <algorithm>
#include <array>

using namespace aether::showcase;

AETHER_TEST("showcase native safety: controller snapshot includes the caller with a creation identity") {
    // Read-only native inspection: never invoke closure, termination or relaunch in this test.
    auto backend = Win32CleanSessionBackend::Create();
    DWORD session = 0;
    REQUIRE(::ProcessIdToSessionId(::GetCurrentProcessId(), &session) != FALSE);
    if (session == 0) {
        CHECK(!backend);  // Service sessions cannot start a user-desktop clean session.
        return;
    }
    REQUIRE(backend);
    const auto controllers = backend->ProtectedControllerProcesses();
    REQUIRE(!controllers.empty());
    CHECK_EQ(controllers.front().processId, ::GetCurrentProcessId());
    CHECK(controllers.front().creationTime != 0);
    for (auto iterator = controllers.begin(); iterator != controllers.end(); ++iterator) {
        CHECK(iterator->processId != 0);
        CHECK(std::none_of(controllers.begin(), iterator, [iterator](const auto& earlier) {
            return earlier.processId == iterator->processId;
        }));
    }
}

AETHER_TEST("showcase native safety: uninitialized backend exposes no controller identities") {
    Win32CleanSessionBackend backend;
    CHECK(backend.ProtectedControllerProcesses().empty());
}

AETHER_TEST("showcase native safety: explicit controller exclusions are exposed without duplicate identities") {
    const std::array<std::uint32_t, 3> retained{::GetCurrentProcessId(), 0xfffffff0U, 0xfffffff0U};
    auto backend = Win32CleanSessionBackend::Create(retained);
    DWORD session = 0;
    REQUIRE(::ProcessIdToSessionId(::GetCurrentProcessId(), &session) != FALSE);
    if (session == 0) {
        CHECK(!backend);
        return;
    }
    REQUIRE(backend);
    const auto controllers = backend->ProtectedControllerProcesses();
    CHECK_EQ(std::count_if(controllers.begin(), controllers.end(), [](const auto& controller) {
        return controller.processId == ::GetCurrentProcessId();
    }), 1);
    const auto retainedController = std::find_if(controllers.begin(), controllers.end(), [](const auto& controller) {
        return controller.processId == 0xfffffff0U;
    });
    REQUIRE(retainedController != controllers.end());
    CHECK_EQ(retainedController->creationTime, 0U);
    CHECK_EQ(std::count_if(controllers.begin(), controllers.end(), [](const auto& controller) {
        return controller.processId == 0xfffffff0U;
    }), 1);
}
