// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "graphics/GraphicsDevice.h"

using namespace aether::graphics;

AETHER_TEST("device recovery: failed attempts back off and end in a bounded exhausted state (AS4-25)") {
    DeviceRecoveryPolicy policy(4, 100);
    CHECK_FALSE(policy.Exhausted());
    CHECK_EQ(*policy.Failed(), 100u);
    CHECK_EQ(*policy.Failed(), 200u);
    CHECK_EQ(*policy.Failed(), 400u);
    CHECK_FALSE(policy.Failed().has_value());  // the fourth failure spends the budget
    CHECK(policy.Exhausted());
    CHECK_FALSE(policy.Failed().has_value());  // and stays spent: no busy loop
    CHECK_EQ(policy.Failures(), 4u);
    policy.Reset();
    CHECK_FALSE(policy.Exhausted());
    CHECK_EQ(*policy.Failed(), 100u);
    policy.Succeeded();
    CHECK_EQ(policy.Failures(), 0u);
}

AETHER_TEST("device recovery: delays are capped and a zero configuration still allows one attempt") {
    DeviceRecoveryPolicy policy(20, 1000);
    unsigned last = 0;
    for (int i = 0; i < 12; ++i) { const auto delay = policy.Failed(); REQUIRE(delay.has_value()); last = *delay; CHECK(*delay <= 8000u); }
    CHECK_EQ(last, 8000u);
    DeviceRecoveryPolicy minimal(0, 0);
    CHECK_FALSE(minimal.Failed().has_value());
    CHECK(minimal.Exhausted());
}
