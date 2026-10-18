// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "graphics/Animation.h"
#include "graphics/GlassPainter.h"
#include "graphics/Theme.h"

using namespace aether::graphics;

AETHER_TEST("animation: easing curves start at 0 and end at 1") {
    for (Easing easing : {Easing::Linear, Easing::OutCubic, Easing::InOutCubic, Easing::OutQuint, Easing::OutBack}) {
        CHECK_NEAR(Ease(easing, 0.0f), 0.0, 1e-6);
        CHECK_NEAR(Ease(easing, 1.0f), 1.0, 1e-6);
        CHECK_NEAR(Ease(easing, -3.0f), 0.0, 1e-6);  // clamped
        CHECK_NEAR(Ease(easing, 7.0f), 1.0, 1e-6);
    }
    CHECK(Ease(Easing::OutCubic, 0.5f) > 0.5f);  // decelerating curve
    CHECK_NEAR(Ease(Easing::InOutCubic, 0.5f), 0.5, 1e-6);
}

AETHER_TEST("animation: tween retargeting is continuous") {
    Tween tween(0.0f);
    tween.AnimateTo(10.0f, 0.0, 1.0, Easing::Linear);
    CHECK_NEAR(tween.Value(0.5), 5.0, 1e-4);
    // Interrupt half-way towards a new target: the value must not jump.
    tween.AnimateTo(-10.0f, 0.5, 1.0, Easing::Linear);
    CHECK_NEAR(tween.Value(0.5), 5.0, 1e-4);
    CHECK_NEAR(tween.Value(1.0), -2.5, 1e-4);
    CHECK_NEAR(tween.Value(5.0), -10.0, 1e-6);
    CHECK_FALSE(tween.IsAnimating(5.0));

    tween.AnimateTo(3.0f, 6.0, 0.0);  // zero duration snaps (reduced motion)
    CHECK_NEAR(tween.Value(6.0), 3.0, 1e-6);
}

AETHER_TEST("animation: spring converges identically at different frame rates") {
    auto simulate = [](double frameRate) {
        Spring spring(0.0f);
        spring.SetTarget(1.0f);
        const double dt = 1.0 / frameRate;
        double elapsed = 0.0;
        while (elapsed < 0.25) {
            spring.Step(dt);
            elapsed += dt;
        }
        return spring.Value();
    };
    const float at60 = simulate(60.0);
    const float at144 = simulate(144.0);
    const float at30 = simulate(30.0);
    CHECK_NEAR(at60, at144, 0.02);
    CHECK_NEAR(at60, at30, 0.03);

    Spring settled(0.0f);
    settled.SetTarget(1.0f);
    for (int i = 0; i < 600 && !settled.IsSettled(); ++i) settled.Step(1.0 / 60.0);
    CHECK(settled.IsSettled());
    CHECK_NEAR(settled.Value(), 1.0, 1e-6);
}

AETHER_TEST("animation: spring tolerates huge frame gaps") {
    Spring spring(0.0f);
    spring.SetTarget(100.0f);
    spring.Step(10.0);  // clamped to 100 ms
    CHECK(std::isfinite(spring.Value()));
    CHECK(spring.Value() <= 120.0f);
}

AETHER_TEST("motion policy: reduced motion zeroes durations") {
    MotionPolicy policy;
    CHECK_NEAR(policy.Scale(0.2), 0.2, 1e-9);
    policy.durationScale = 1.5f;
    CHECK_NEAR(policy.Scale(0.2), 0.3, 1e-6);
    policy.reduced = true;
    CHECK_NEAR(policy.Scale(0.2), 0.0, 0.0);
}

AETHER_TEST("theme: glass tint stays readable without blur") {
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light}) {
        const auto tokens = MakeDesignTokens(mode);
        GlassStyle style;
        style.opacity = 0.3f;
        style.backdropBlur = false;
        CHECK(GlassTintAlpha(tokens, style) >= 0.72f);
        style.backdropBlur = true;
        const float clear = GlassTintAlpha(tokens, style);
        style.opacity = 1.0f;
        CHECK(GlassTintAlpha(tokens, style) > clear);
    }
    const auto contrast = MakeDesignTokens(ThemeMode::HighContrast);
    CHECK_NEAR(GlassTintAlpha(contrast, GlassStyle{}), 1.0, 0.0);
}
