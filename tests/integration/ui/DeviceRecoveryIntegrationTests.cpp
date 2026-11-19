// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// A02: a rendering device that cannot be recovered says so once its budget is spent (the host then gives the Windows
// taskbar back), and a fresh budget that succeeds replaces the device (the host then resumes).
#include "AetherTest.h"

#include "graphics/GraphicsDevice.h"
#include "platform/windows/ComApartment.h"

#include <chrono>
#include <thread>

using namespace aether;

namespace {
void PumpFor(std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
}  // namespace

AETHER_TEST("device recovery integration: a device that stays lost is given up once, then a fresh budget replaces it (A02)") {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    graphics::GraphicsDevice device;
    REQUIRE(device.Initialize().ok());
    int gaveUp = 0, replaced = 0;
    auto giveUp = device.RecoveryGaveUp.Connect([&] { ++gaveUp; });
    auto replace = device.DeviceReplaced.Connect([&] { ++replaced; });
    device.SimulateFailedRecoveriesForTests(10, graphics::DeviceRecoveryPolicy(3, 5));
    device.RequestRecovery();                       // fails now, then twice more on its own timer
    PumpFor(std::chrono::milliseconds(400));
    CHECK(device.RecoveryExhausted());
    CHECK_EQ(gaveUp, 1);
    CHECK_EQ(replaced, 0);
    device.RequestRecovery();                       // spent: nothing more until a reset, and no second notice
    PumpFor(std::chrono::milliseconds(50));
    CHECK_EQ(gaveUp, 1);
    device.SimulateFailedRecoveriesForTests(0, graphics::DeviceRecoveryPolicy(3, 5));
    device.RequestRecovery();                       // a display change or a resume: a fresh budget that succeeds
    CHECK_EQ(replaced, 1);
    CHECK_FALSE(device.RecoveryExhausted());
    device.Shutdown();
}
