// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Window transition presets and the design-language timing curves used by Direct2D tweens. Pure:
// no window, compositor or device.
#include "AetherTest.h"

#include "graphics/Animation.h"
#include "graphics/Theme.h"
#include "graphics/Transition.h"

#include <cmath>
#include <limits>

using namespace aether::graphics;

AETHER_TEST("transition: the enter preset fades in from its shape over the normal duration with the decelerate curve") {
    const auto tokens = MakeDesignTokens(ThemeMode::Dark);
    TransitionShape from;
    from.offsetY = 12.0f;
    from.scale = 0.97f;
    from.centerX = 340.0f;
    from.centerY = 8.0f;
    const auto enter = MakeEnterTransition(from, tokens, MotionPolicy{});
    CHECK_EQ(enter.opacityFrom, 0.0f);
    CHECK_EQ(enter.opacityTo, 1.0f);
    CHECK_EQ(enter.offsetFromY, 12.0f);
    CHECK_EQ(enter.offsetToY, 0.0f);
    CHECK_EQ(enter.scaleFrom, 0.97f);
    CHECK_EQ(enter.scaleTo, 1.0f);
    CHECK_EQ(enter.centerX, 340.0f);
    CHECK_NEAR(enter.duration, tokens.durationNormal, 1e-12);
    CHECK_EQ(enter.curve.x1, tokens.curves.decelerate.x1);
    CHECK_EQ(enter.curve.y2, tokens.curves.decelerate.y2);
    CHECK_FALSE(enter.IsInstant());
}

AETHER_TEST("transition: the exit preset fades out towards its shape over the fast duration with the accelerate curve") {
    const auto tokens = MakeDesignTokens(ThemeMode::Light);
    TransitionShape to;
    to.offsetY = 40.0f;
    to.scale = 0.98f;
    const auto exit = MakeExitTransition(to, tokens, MotionPolicy{});
    CHECK_EQ(exit.opacityFrom, 1.0f);
    CHECK_EQ(exit.opacityTo, 0.0f);
    CHECK_EQ(exit.offsetFromY, 0.0f);
    CHECK_EQ(exit.offsetToY, 40.0f);
    CHECK_EQ(exit.scaleFrom, 1.0f);
    CHECK_EQ(exit.scaleTo, 0.98f);
    CHECK_NEAR(exit.duration, tokens.durationFast, 1e-12);
    CHECK_EQ(exit.curve.x1, tokens.curves.accelerate.x1);
    CHECK_EQ(exit.curve.x2, tokens.curves.accelerate.x2);
}

AETHER_TEST("transition: durations follow the animation speed, and reduced motion or high contrast are instant") {
    const auto tokens = MakeDesignTokens(ThemeMode::Dark);
    MotionPolicy slower;
    slower.durationScale = 1.5f;
    CHECK_NEAR(MakeEnterTransition({}, tokens, slower).duration, tokens.durationNormal * 1.5, 1e-6);
    MotionPolicy faster;
    faster.durationScale = 0.1f;  // clamped to 0.5
    CHECK_NEAR(MakeExitTransition({}, tokens, faster).duration, tokens.durationFast * 0.5, 1e-6);

    MotionPolicy reduced;
    reduced.reduced = true;
    CHECK(MakeEnterTransition({}, tokens, reduced).IsInstant());
    CHECK(MakeExitTransition({}, tokens, reduced).IsInstant());
    const auto contrast = MakeDesignTokens(ThemeMode::HighContrast);
    CHECK(MakeEnterTransition({}, contrast, MotionPolicy{}).IsInstant());
    CHECK(MakeExitTransition({}, contrast, MotionPolicy{}).IsInstant());
    // The instant variants still describe the end state, which the surface applies at once.
    CHECK_EQ(MakeExitTransition({}, contrast, MotionPolicy{}).opacityTo, 0.0f);
    CHECK_EQ(MakeEnterTransition({}, contrast, MotionPolicy{}).opacityTo, 1.0f);
}

AETHER_TEST("transition: invalid shapes and durations are sanitized") {
    const auto tokens = MakeDesignTokens(ThemeMode::Dark);
    TransitionShape broken;
    broken.offsetX = std::numeric_limits<float>::quiet_NaN();
    broken.offsetY = std::numeric_limits<float>::infinity();
    broken.scale = -2.0f;
    broken.centerX = std::numeric_limits<float>::quiet_NaN();
    const auto enter = MakeEnterTransition(broken, tokens, MotionPolicy{});
    CHECK_EQ(enter.offsetFromX, 0.0f);
    CHECK_EQ(enter.offsetFromY, 0.0f);
    CHECK_EQ(enter.scaleFrom, 1.0f);
    CHECK_EQ(enter.centerX, 0.0f);
    const auto exit = MakeExitTransition(broken, tokens, MotionPolicy{});
    CHECK_EQ(exit.scaleTo, 1.0f);
    CHECK_EQ(exit.offsetToY, 0.0f);

    Transition transition;
    transition.duration = 0.0;
    CHECK(transition.IsInstant());
    transition.duration = -1.0;
    CHECK(transition.IsInstant());
    transition.duration = std::numeric_limits<double>::quiet_NaN();
    CHECK(transition.IsInstant());
    transition.duration = 60.0;  // beyond the 10 s limit of the compositor wrappers
    CHECK(transition.IsInstant());
    transition.duration = 0.25;
    CHECK_FALSE(transition.IsInstant());
}

AETHER_TEST("motion: the design-language easing curves match their cubic-bezier tokens") {
    const CurveTokens curves;
    for (float t : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f}) {
        CHECK_NEAR(Ease(Easing::Standard, t), EvaluateCubicBezier(curves.standard, t), 1e-6);
        CHECK_NEAR(Ease(Easing::Decelerate, t), EvaluateCubicBezier(curves.decelerate, t), 1e-6);
        CHECK_NEAR(Ease(Easing::Accelerate, t), EvaluateCubicBezier(curves.accelerate, t), 1e-6);
    }
    // Decelerate is ahead of linear, accelerate behind it.
    CHECK(Ease(Easing::Decelerate, 0.3f) > 0.3f);
    CHECK(Ease(Easing::Accelerate, 0.3f) < 0.3f);
    CHECK_NEAR(Ease(Easing::Standard, 0.0f), 0.0, 1e-6);
    CHECK_NEAR(Ease(Easing::Standard, 1.0f), 1.0, 1e-6);
}

AETHER_TEST("motion: a tween can follow a cubic-bezier curve and retargets continuously") {
    const CurveTokens curves;
    Tween tween(0.0f);
    tween.AnimateTo(100.0f, 1.0, 0.5, curves.decelerate);
    CHECK(tween.IsAnimating(1.2));
    CHECK_NEAR(tween.Value(1.0), 0.0, 1e-4);
    CHECK_NEAR(tween.Value(1.25), 100.0f * EvaluateCubicBezier(curves.decelerate, 0.5f), 1e-3);
    CHECK_NEAR(tween.Value(1.5), 100.0, 1e-6);
    CHECK_FALSE(tween.IsAnimating(1.5));

    tween.AnimateTo(0.0f, 2.0, 0.4, curves.standard);
    const float midway = tween.Value(2.2);
    tween.AnimateTo(50.0f, 2.2, 0.4, Easing::Linear);  // interrupt with an Easing: no jump
    CHECK_NEAR(tween.Value(2.2), midway, 1e-4);
    CHECK_NEAR(tween.Value(2.4), (midway + 50.0f) / 2.0f, 1e-3);

    tween.AnimateTo(7.0f, 3.0, 0.0, curves.accelerate);  // zero duration snaps (reduced motion)
    CHECK_NEAR(tween.Value(3.0), 7.0, 1e-6);
}

AETHER_TEST("motion policy: reduced motion forbids decorative movement") {
    MotionPolicy policy;
    CHECK(policy.AllowsMovement());
    policy.reduced = true;
    CHECK_FALSE(policy.AllowsMovement());
    CHECK_NEAR(policy.Scale(0.3), 0.0, 0.0);
}
