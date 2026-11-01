// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "platform/windows/SystemEvents.h"

#include <chrono>
#include <functional>
#include <vector>

using namespace aether;
using namespace aether::platform;

namespace {
bool PumpUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        if (predicate()) return true;
        if (std::chrono::steady_clock::now() >= end) return predicate();
        ::Sleep(2);
    }
}
}  // namespace

AETHER_TEST("system events: session registration retries after an early failure and pairs unregistration (AS4-22)") {
    int attempts = 0, unregistered = 0;
    HWND registeredWindow = nullptr;
    SessionRegistrar registrar;
    registrar.registerWindow = [&](HWND window) { if (++attempts < 3) return false; registeredWindow = window; return true; };
    registrar.unregisterWindow = [&](HWND window) { ++unregistered; CHECK(window == registeredWindow); };
    registrar.firstRetryMs = 5;
    registrar.maximumAttempts = 5;
    SystemEvents events(registrar);
    std::vector<SessionNotifications> changes;
    auto connection = events.SessionNotificationStateChanged.Connect([&](SessionNotifications state) { changes.push_back(state); });
    REQUIRE(events.Create().ok());
    CHECK(events.SessionNotificationState() == SessionNotifications::Pending);
    REQUIRE(PumpUntil([&] { return events.SessionNotificationState() == SessionNotifications::Registered; }, std::chrono::seconds(3)));
    CHECK_EQ(attempts, 3);
    REQUIRE_EQ(changes.size(), 1u);
    CHECK(changes[0] == SessionNotifications::Registered);
    // No further attempts once registered.
    (void)PumpUntil([] { return false; }, std::chrono::milliseconds(60));
    CHECK_EQ(attempts, 3);
    events.Destroy();
    CHECK_EQ(unregistered, 1);
}

AETHER_TEST("system events: a permanent session registration failure is reported Unavailable, never registered") {
    int attempts = 0, unregistered = 0;
    SessionRegistrar registrar;
    registrar.registerWindow = [&](HWND) { ++attempts; return false; };
    registrar.unregisterWindow = [&](HWND) { ++unregistered; };
    registrar.firstRetryMs = 2;
    registrar.maximumAttempts = 4;
    SystemEvents events(registrar);
    std::vector<SessionNotifications> changes;
    auto connection = events.SessionNotificationStateChanged.Connect([&](SessionNotifications state) { changes.push_back(state); });
    REQUIRE(events.Create().ok());
    REQUIRE(PumpUntil([&] { return events.SessionNotificationState() == SessionNotifications::Unavailable; }, std::chrono::seconds(3)));
    CHECK_EQ(attempts, 4);
    CHECK_EQ(events.SessionRegistrationAttempts(), 4u);
    REQUIRE_EQ(changes.size(), 1u);
    CHECK(changes[0] == SessionNotifications::Unavailable);
    (void)PumpUntil([] { return false; }, std::chrono::milliseconds(60));
    CHECK_EQ(attempts, 4);
    events.Destroy();
    CHECK_EQ(unregistered, 0);
}

AETHER_TEST("system events: shutdown during a session registration retry cancels further attempts") {
    int attempts = 0;
    SessionRegistrar registrar;
    registrar.registerWindow = [&](HWND) { ++attempts; return false; };
    registrar.unregisterWindow = [](HWND) {};
    registrar.firstRetryMs = 40;
    registrar.maximumAttempts = 8;
    SystemEvents events(registrar);
    REQUIRE(events.Create().ok());
    CHECK_EQ(attempts, 1);
    events.Destroy();
    (void)PumpUntil([] { return false; }, std::chrono::milliseconds(200));
    CHECK_EQ(attempts, 1);
}
