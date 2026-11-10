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

AETHER_TEST("dock indicator: hover expands to exactly the foreground width at a calm, readable pace") {
    DockIndicatorMotion background, foreground;
    const aether::graphics::MotionPolicy policy;
    foreground.Retarget(true, true, false, policy);
    background.Retarget(true, false, false, policy);
    CHECK_EQ(foreground.Width(), kDockMarkerDotWidth);  // first observation starts as a dot...
    SettleIndicator(foreground);
    CHECK_EQ(foreground.Width(), kDockMarkerPillWidth);
    background.Retarget(true, false, true, policy);
    double elapsed = 0, half = -1, ninety = -1;
    while (background.IsAnimating() && elapsed < 2) {
        background.Step(1.0 / 120); elapsed += 1.0 / 120;
        const float progress = (background.Width() - kDockMarkerDotWidth) / (kDockMarkerPillWidth - kDockMarkerDotWidth);
        if (half < 0 && progress >= 0.5f) half = elapsed;
        if (ninety < 0 && progress >= 0.9f) ninety = elapsed;
        CHECK(background.Width() <= kDockMarkerPillWidth + 0.01f);  // no visible overshoot
    }
    CHECK(half > 0.12); CHECK(half < 0.22);
    CHECK(ninety > 0.30); CHECK(ninety < 0.45);  // the owner's 300-450 ms proposal, at normal speed
    CHECK(elapsed < 1.0);  // and frame demand still ends
    CHECK_EQ(background.Width(), foreground.Width());
    aether::graphics::MotionPolicy slower; slower.durationScale = 1.5f;
    DockIndicatorMotion scaled; scaled.Retarget(true, false, false, slower); scaled.Retarget(true, false, true, slower);
    scaled.Step(half);
    CHECK((scaled.Width() - kDockMarkerDotWidth) / (kDockMarkerPillWidth - kDockMarkerDotWidth) < 0.5f);
}

AETHER_TEST("dock indicator: observed activation of a hovered marker continues without a restart") {
    DockIndicatorMotion previous, hovered;
    const aether::graphics::MotionPolicy policy;
    previous.Retarget(true, true, false, policy); SettleIndicator(previous);
    hovered.Retarget(true, false, false, policy);
    hovered.Retarget(true, false, true, policy); SettleIndicator(hovered);
    // Foreground moves from the old application to the hovered one in the same observation.
    previous.Retarget(true, false, false, policy);
    hovered.Retarget(true, true, true, policy);
    CHECK(previous.IsAnimating());
    CHECK_FALSE(hovered.IsAnimating());
    float last = previous.Width();
    for (int frame = 0; frame < 12; ++frame) {
        previous.Step(1.0 / 60); hovered.Step(1.0 / 60);
        CHECK(previous.Width() < last); last = previous.Width();
        CHECK_EQ(hovered.Width(), kDockMarkerPillWidth);
    }
    // A partially expanded hover marker keeps its width and velocity through the same transfer.
    DockIndicatorMotion partial; partial.Retarget(true, false, false, policy); partial.Retarget(true, false, true, policy);
    partial.Step(0.1);
    const float before = partial.Width();
    partial.Retarget(true, true, false, policy);
    CHECK_EQ(partial.Width(), before);
    partial.Step(1.0 / 60);
    CHECK(partial.Width() > before);
}

AETHER_TEST("dock motion: a dragged icon stays in the Dock, which stretches after it like a rubber band (D73)") {
    // The band follows one for one at first, then less and less, and never reaches its limit.
    CHECK_EQ(DockRubberBand(0, kDockElasticReach), 0.0f);
    CHECK_EQ(DockRubberBand(-30, kDockElasticReach), 0.0f);
    CHECK_EQ(DockRubberBand(30, 0), 0.0f);
    CHECK(DockRubberBand(1, kDockElasticReach) > 0.95f);
    float previous = 0, previousGain = 1;
    for (int pull = 1; pull <= 4000; pull += pull < 100 ? 1 : 97) {
        const float followed = DockRubberBand(static_cast<float>(pull), kDockElasticReach);
        CHECK(followed > previous);                       // pulling further always stretches a little more
        CHECK(followed < kDockElasticReach);              // but never beyond the Dock's own room
        CHECK(followed < static_cast<float>(pull));
        const float gain = followed / static_cast<float>(pull);
        CHECK(gain <= previousGain + 0.0001f);            // with more and more resistance
        previous = followed; previousGain = gain;
    }
    CHECK(DockRubberBand(4000, kDockElasticReach) > kDockElasticReach - 0.5f);
    // Between the panel's ends the icon is where the hand is, and nothing stretches.
    const float low = 120, high = 480;
    for (const float wanted : {120.0f, 121.5f, 300.0f, 480.0f}) CHECK((PullDockIcon(wanted, low, high, kDockElasticReach) == DockPull{wanted, 0, 0}));
    // Past an end the icon goes only as far as that end stretches, however far the hand goes: never out of the Dock.
    for (const float beyond : {1.0f, 10.0f, 60.0f, 500.0f, 20000.0f}) {
        const auto right = PullDockIcon(high + beyond, low, high, kDockElasticReach);
        CHECK(right.stretchRight > 0); CHECK(right.stretchLeft == 0);
        CHECK(std::abs(right.center - (high + right.stretchRight)) < 0.001f);
        CHECK(right.center < high + kDockElasticReach);
        const auto left = PullDockIcon(low - beyond, low, high, kDockElasticReach);
        CHECK(left.stretchLeft > 0); CHECK(left.stretchRight == 0);
        CHECK(std::abs(left.center - (low - left.stretchLeft)) < 0.001f);
        CHECK(left.center > low - kDockElasticReach);
        CHECK(std::abs(left.stretchLeft - right.stretchRight) < 0.001f);   // the same band on both sides
    }
    // Without motion (reduced motion, high contrast) the icon simply stops at the ends.
    CHECK((PullDockIcon(900, low, high, 0) == DockPull{high, 0, 0}));
    CHECK((PullDockIcon(-900, low, high, 0) == DockPull{low, 0, 0}));
    // Senseless input never puts the icon somewhere senseless.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK((PullDockIcon(nan, low, high, kDockElasticReach) == DockPull{low, 0, 0}));
    CHECK(PullDockIcon(300, 480, 120, kDockElasticReach).center > 120 - kDockElasticReach);
    CHECK(PullDockIcon(300, 480, 120, kDockElasticReach).center < 480 + kDockElasticReach);
}
