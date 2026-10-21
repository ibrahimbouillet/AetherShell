// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "dock/DockLayout.h"
#include "dock/DockMotion.h"
#include "graphics/Animation.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace aether::dock;

AETHER_TEST("dock motion: stable slot centers require a full crossing and reverse without oscillation") {
    const std::vector<float> centers{100, 156, 212, 268};
    CHECK_EQ(*CrossedDockSlot(centers, 0, 155.9f), 0u);
    CHECK_EQ(*CrossedDockSlot(centers, 0, 156), 1u);
    CHECK_EQ(*CrossedDockSlot(centers, 1, 156), 1u);
    CHECK_EQ(*CrossedDockSlot(centers, 1, 100.1f), 1u);
    CHECK_EQ(*CrossedDockSlot(centers, 1, 100), 0u);
    CHECK_EQ(*CrossedDockSlot(centers, 0, 300), 3u);
    CHECK_EQ(*CrossedDockSlot(centers, 3, -100), 0u);
    CHECK_FALSE(CrossedDockSlot(centers, 4, 100).has_value());
    CHECK_FALSE(CrossedDockSlot(centers, 0, std::numeric_limits<float>::quiet_NaN()).has_value());
    CHECK_FALSE(CrossedDockSlot(std::vector<float>{100, 100}, 0, 100).has_value());
}

AETHER_TEST("dock motion: rebasing an in-flight neighbour retains its position and momentum") {
    aether::graphics::Spring offset(56, aether::graphics::Motion().slide); offset.SetTarget(0);
    offset.Step(0.035);
    const float oldBase = 156, newBase = 212;
    const float screen = oldBase + offset.Value(), velocity = offset.Velocity();
    offset.ShiftBy(oldBase - newBase); offset.SetTarget(0);
    CHECK(std::abs(newBase + offset.Value() - screen) < 0.0001f);
    CHECK_EQ(offset.Velocity(), velocity);
    offset.Step(0.015);
    CHECK(newBase + offset.Value() < screen);
}

AETHER_TEST("dock motion: captured original item owns overlapping input above later neighbours") {
    DockLayout layout;
    layout.items = {{{10, 10, 70, 70}, 1}, {{30, 10, 90, 70}, 1}};
    CHECK_EQ(*HitTest(layout, {50, 30}), 1u);
    layout.raisedItem = 0;
    CHECK_EQ(*HitTest(layout, {50, 30}), 0u);
    CHECK_EQ(*HitTest(layout, {80, 30}), 1u);
}

AETHER_TEST("dock motion: attention pulse is bounded finite and returns exactly to rest") {
    float maximumOpacity = 0, maximumHeight = 0;
    for (int i = 0; i <= 2600; ++i) {
        const auto value = DockAttentionAt(static_cast<double>(i) / 1000);
        CHECK(value.opacity >= 0); CHECK(value.opacity <= 0.65f);
        CHECK(value.height >= 0); CHECK(value.height <= 6);
        maximumOpacity = std::max(maximumOpacity, value.opacity);
        maximumHeight = std::max(maximumHeight, value.height);
    }
    CHECK(maximumOpacity > 0.4f); CHECK(maximumHeight > 4);
    CHECK_EQ(DockAttentionAt(0).opacity, 0.0f);
    CHECK_EQ(DockAttentionAt(kDockAttentionDuration).height, 0.0f);
    CHECK_EQ(DockAttentionAt(-1).opacity, 0.0f);
    CHECK_EQ(DockAttentionAt(std::numeric_limits<double>::infinity()).height, 0.0f);
}
