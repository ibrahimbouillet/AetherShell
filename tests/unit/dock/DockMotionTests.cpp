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

namespace {
void SettleIndicator(DockIndicatorMotion& marker) {
    for (int frame = 0; frame < 600 && marker.IsAnimating(); ++frame) marker.Step(1.0 / 120);
    CHECK_FALSE(marker.IsAnimating());
}
}

AETHER_TEST("dock indicator: closed pins never show a marker from hover or an unobserved activation") {
    DockIndicatorMotion marker;
    const aether::graphics::MotionPolicy policy;
    marker.Retarget(false, false, true, policy);
    CHECK_EQ(marker.Width(), 0.0f);
    CHECK_FALSE(marker.IsAnimating());
    marker.Retarget(false, true, true, policy); // inconsistent foreground input cannot invent running state
    marker.Step(0.05);
    CHECK_EQ(marker.Width(), 0.0f);
    CHECK_FALSE(marker.IsAnimating());
}

AETHER_TEST("dock indicator: running hover expands continuously and foreground survives pointer leave") {
    DockIndicatorMotion marker;
    const aether::graphics::MotionPolicy policy;
    marker.Retarget(true, false, false, policy);
    CHECK_EQ(marker.Width(), 5.0f);
    CHECK_FALSE(marker.IsAnimating());
    marker.Retarget(true, false, true, policy);
    CHECK_EQ(marker.Width(), 5.0f);
    CHECK(marker.IsAnimating());
    marker.Step(0.03);
    CHECK(marker.Width() > 5); CHECK(marker.Width() < 12);
    SettleIndicator(marker);
    CHECK_EQ(marker.Width(), 12.0f);
    marker.Retarget(true, true, false, policy);
    CHECK_EQ(marker.Width(), 12.0f);
    CHECK_FALSE(marker.IsAnimating());
    marker.Retarget(true, false, false, policy);
    CHECK_EQ(marker.Width(), 12.0f);
    SettleIndicator(marker);
    CHECK_EQ(marker.Width(), 5.0f);
}

AETHER_TEST("dock indicator: rapid reversal and foreground retarget keep in-flight width and momentum") {
    DockIndicatorMotion marker;
    const aether::graphics::MotionPolicy policy;
    marker.Retarget(true, false, true, policy);
    marker.Step(0.03);
    const float expanding = marker.Width();
    marker.Retarget(true, false, false, policy);
    CHECK_EQ(marker.Width(), expanding);
    marker.Step(0.001);
    CHECK(marker.Width() > expanding); // the velocity is retained rather than reset at reversal
    const float reversed = marker.Width();
    marker.Retarget(true, true, false, policy);
    CHECK_EQ(marker.Width(), reversed);
    SettleIndicator(marker);
    CHECK_EQ(marker.Width(), 12.0f);
    marker.Retarget(false, false, true, policy);
    CHECK_EQ(marker.Width(), 0.0f); // observed closure removes the cue immediately
    CHECK_FALSE(marker.IsAnimating());
    marker.Retarget(true, false, false, policy);
    CHECK_EQ(marker.Width(), 5.0f); // a new observed run starts as a dot, without stale pill velocity
    CHECK_FALSE(marker.IsAnimating());
}

AETHER_TEST("dock indicator: reduced motion snaps both directions and stops frame demand") {
    DockIndicatorMotion marker;
    aether::graphics::MotionPolicy policy;
    marker.Retarget(true, false, true, policy); marker.Step(0.02);
    CHECK(marker.IsAnimating());
    policy.reduced = true;
    marker.Retarget(true, false, true, policy);
    CHECK_EQ(marker.Width(), 12.0f); CHECK_FALSE(marker.IsAnimating());
    marker.Retarget(true, false, false, policy);
    CHECK_EQ(marker.Width(), 5.0f); CHECK_FALSE(marker.IsAnimating());
    marker.Retarget(false, false, true, policy);
    CHECK_EQ(marker.Width(), 0.0f); CHECK_FALSE(marker.IsAnimating());
}

AETHER_TEST("dock indicator: repeated observations preserve the curve at different frame cadences") {
    DockIndicatorMotion first, second;
    const aether::graphics::MotionPolicy policy;
    first.Retarget(true, false, true, policy); second.Retarget(true, false, true, policy);
    for (int frame = 0; frame < 30; ++frame) {
        first.Step(0.01);
        first.Retarget(true, false, true, policy);
    }
    for (int frame = 0; frame < 15; ++frame) second.Step(0.02);
    CHECK(std::abs(first.Width() - second.Width()) < 0.001f);
    SettleIndicator(first); SettleIndicator(second);
    CHECK_EQ(first.Width(), second.Width());
    for (int sample = 0; sample < 100; ++sample) {
        first.Retarget(true, false, true, policy);
        CHECK_FALSE(first.IsAnimating());
    }
}
