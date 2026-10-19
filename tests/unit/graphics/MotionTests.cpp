// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Timing curves, spring presets, the shared motion table and the launch bounce.
#include "AetherTest.h"

#include "graphics/Animation.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace aether::graphics;

namespace {

// Reference solution by plain bisection in double precision.
double ReferenceBezier(double x1, double y1, double x2, double y2, double t) {
    const auto coordinate = [](double a1, double a2, double u) {
        const double v = 1.0 - u;
        return 3.0 * v * v * u * a1 + 3.0 * v * u * u * a2 + u * u * u;
    };
    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < 200; ++i) {
        const double middle = (low + high) / 2.0;
        if (coordinate(x1, x2, middle) < t) low = middle;
        else high = middle;
    }
    return coordinate(y1, y2, (low + high) / 2.0);
}

struct SpringRun {
    float maximum = 0.0f;
    double settledAfter = -1.0;
};

// Steps a spring from 0 towards 1 at 60 Hz and reports its peak and settle time.
SpringRun Run(Spring::Parameters parameters, double limit = 3.0) {
    Spring spring(0.0f, parameters);
    spring.SetTarget(1.0f);
    SpringRun run;
    for (double elapsed = 0.0; elapsed < limit; elapsed += 1.0 / 60.0) {
        spring.Step(1.0 / 60.0);
        run.maximum = std::max(run.maximum, spring.Value());
        if (spring.IsSettled()) {
            run.settledAfter = elapsed + 1.0 / 60.0;
            break;
        }
    }
    return run;
}

}  // namespace

AETHER_TEST("motion: cubic bezier hits its end points exactly and clamps time") {
    for (const CubicBezier curve : {CubicBezier{0.2f, 0.0f, 0.0f, 1.0f}, CubicBezier{0.0f, 0.0f, 0.0f, 1.0f},
                                    CubicBezier{0.3f, 0.0f, 1.0f, 1.0f}, CubicBezier{0.34f, 1.56f, 0.64f, 1.0f}}) {
        CHECK_EQ(EvaluateCubicBezier(curve, 0.0f), 0.0f);
        CHECK_EQ(EvaluateCubicBezier(curve, 1.0f), 1.0f);
        CHECK_EQ(EvaluateCubicBezier(curve, -2.0f), 0.0f);
        CHECK_EQ(EvaluateCubicBezier(curve, 4.0f), 1.0f);
        CHECK_EQ(EvaluateCubicBezier(curve, std::numeric_limits<float>::quiet_NaN()), 0.0f);
    }
}

AETHER_TEST("motion: cubic bezier matches a bisection reference within 1e-5") {
    const CubicBezier curves[] = {
        {0.2f, 0.0f, 0.0f, 1.0f},     // standard
        {0.0f, 0.0f, 0.0f, 1.0f},     // decelerate
        {0.3f, 0.0f, 1.0f, 1.0f},     // accelerate
        {0.25f, 0.1f, 0.25f, 1.0f},   // CSS ease
        {0.42f, 0.0f, 0.58f, 1.0f},   // CSS ease-in-out
        {0.34f, 1.56f, 0.64f, 1.0f},  // overshooting
        {1.0f, 0.0f, 0.0f, 1.0f},     // flat middle slope
        {0.0f, 1.0f, 1.0f, 0.0f},     // flat end slopes
    };
    for (const auto& curve : curves) {
        for (int step = 1; step < 200; ++step) {
            const float t = static_cast<float>(step) / 200.0f;
            const double expected = ReferenceBezier(curve.x1, curve.y1, curve.x2, curve.y2, t);
            CHECK_NEAR(EvaluateCubicBezier(curve, t), expected, 1e-5);
        }
    }
    CHECK_NEAR(EvaluateCubicBezier({0.25f, 0.1f, 0.25f, 1.0f}, 0.5f), 0.8024033, 1e-5);
}

AETHER_TEST("motion: cubic bezier on the diagonal is linear and the token curves are monotonic") {
    for (int step = 0; step <= 100; ++step) {
        const float t = static_cast<float>(step) / 100.0f;
        CHECK_NEAR(EvaluateCubicBezier({0.25f, 0.25f, 0.75f, 0.75f}, t), t, 1e-5);
    }
    for (const CubicBezier curve : {CubicBezier{0.2f, 0.0f, 0.0f, 1.0f}, CubicBezier{0.0f, 0.0f, 0.0f, 1.0f},
                                    CubicBezier{0.3f, 0.0f, 1.0f, 1.0f}}) {
        float previous = 0.0f;
        for (int step = 1; step <= 500; ++step) {
            const float value = EvaluateCubicBezier(curve, static_cast<float>(step) / 500.0f);
            CHECK(value + 1e-6f >= previous);
            previous = value;
        }
    }
    CHECK(EvaluateCubicBezier({0.0f, 0.0f, 0.0f, 1.0f}, 0.5f) > 0.5f);  // decelerating
    CHECK(EvaluateCubicBezier({0.3f, 0.0f, 1.0f, 1.0f}, 0.5f) < 0.5f);  // accelerating
    // An overshooting curve may leave 0..1 in between.
    float peak = 0.0f;
    for (int step = 0; step <= 100; ++step) peak = std::max(peak, EvaluateCubicBezier({0.34f, 1.56f, 0.64f, 1.0f}, step / 100.0f));
    CHECK(peak > 1.0f);
}

AETHER_TEST("motion: cubic bezier sanitizes out-of-range and non-finite control points") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int step = 0; step <= 20; ++step) {
        const float t = static_cast<float>(step) / 20.0f;
        // x outside 0..1 is clamped, which keeps the curve a function of time.
        CHECK_NEAR(EvaluateCubicBezier({-3.0f, 0.0f, 5.0f, 1.0f}, t), EvaluateCubicBezier({0.0f, 0.0f, 1.0f, 1.0f}, t), 1e-6);
        const float sanitized = EvaluateCubicBezier({nan, nan, inf, -inf}, t);
        CHECK(std::isfinite(sanitized));
        CHECK_NEAR(sanitized, t, 1e-5);
    }
}

AETHER_TEST("motion: springs from a damping ratio use 2 * ratio * sqrt(stiffness * mass)") {
    const auto press = SpringFromDampingRatio(900.0f, 0.75f);
    CHECK_NEAR(press.stiffness, 900.0, 1e-4);
    CHECK_NEAR(press.damping, 45.0, 1e-4);
    CHECK_NEAR(press.mass, 1.0, 0.0);
    const auto heavy = SpringFromDampingRatio(400.0f, 1.0f, 4.0f);
    CHECK_NEAR(heavy.damping, 80.0, 1e-4);
    CHECK_NEAR(heavy.mass, 4.0, 0.0);
    // Critically damped: no overshoot. Under-damped: a slight overshoot, then rest.
    const auto critical = Run(SpringFromDampingRatio(600.0f, 1.0f));
    CHECK(critical.maximum < 1.005f);
    CHECK(critical.settledAfter > 0.0);
    const auto bouncy = Run(SpringFromDampingRatio(600.0f, 0.5f));
    CHECK(bouncy.maximum > 1.05f);
    CHECK(bouncy.settledAfter > 0.0);
    // Invalid input falls back to defaults and a ratio that still settles.
    const Spring::Parameters defaults;
    const auto invalid = SpringFromDampingRatio(-1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f);
    CHECK_EQ(invalid.stiffness, defaults.stiffness);
    CHECK_EQ(invalid.mass, defaults.mass);
    CHECK_NEAR(invalid.damping, 2.0 * std::sqrt(420.0), 1e-3);
    const auto undamped = SpringFromDampingRatio(420.0f, 0.0f);
    CHECK(undamped.damping > 0.0f);
    CHECK(Run(undamped, 30.0).settledAfter > 0.0);
}

AETHER_TEST("motion: the shared motion table holds the resolved presets") {
    const auto& motion = Motion();
    CHECK_EQ(motion.magnify.stiffness, 420.0f);
    CHECK_EQ(motion.magnify.damping, 38.0f);
    CHECK_EQ(motion.press.stiffness, 900.0f);
    CHECK_NEAR(motion.press.damping, 2.0 * 0.75 * std::sqrt(900.0), 1e-4);
    CHECK_EQ(motion.lift.stiffness, 600.0f);
    CHECK_NEAR(motion.lift.damping, 2.0 * 0.8 * std::sqrt(600.0), 1e-4);
    CHECK_EQ(motion.slide.stiffness, 380.0f);
    CHECK_NEAR(motion.slide.damping, 2.0 * 0.85 * std::sqrt(380.0), 1e-4);
    CHECK_NEAR(motion.longPress, 0.350, 1e-12);
    CHECK_EQ(motion.liftOffset, 8.0f);
    CHECK_EQ(motion.liftScale, 1.08f);
    CHECK_EQ(motion.pressScale, 0.92f);
    CHECK_EQ(&Motion(), &motion);
    // The gesture springs overshoot only slightly (realistic, not bouncy) and settle well within a second.
    for (const auto& parameters : {motion.press, motion.lift, motion.slide}) {
        const auto run = Run(parameters);
        CHECK(run.maximum > 1.0f);
        CHECK(run.maximum < 1.05f);
        CHECK(run.settledAfter > 0.0);
        CHECK(run.settledAfter < 1.0);
    }
}

AETHER_TEST("motion policy: springs and durations follow the clamped animation speed") {
    MotionPolicy policy;
    const auto base = Motion().slide;
    const auto same = policy.ScaleSpring(base);
    CHECK_EQ(same.stiffness, base.stiffness);
    CHECK_EQ(same.damping, base.damping);
    policy.durationScale = 1.5f;
    const auto slow = policy.ScaleSpring(base);
    CHECK_NEAR(slow.stiffness, base.stiffness / 2.25, 1e-3);
    CHECK_NEAR(slow.damping, base.damping / 1.5, 1e-4);
    CHECK_NEAR(slow.restVelocity, base.restVelocity / 1.5, 1e-7);
    // A slower spring traces the same curve stretched in time.
    const auto baseRun = Run(base);
    const auto slowRun = Run(slow);
    CHECK_NEAR(slowRun.maximum, baseRun.maximum, 0.01);
    CHECK_NEAR(slowRun.settledAfter, baseRun.settledAfter * 1.5, 0.1);
    policy.durationScale = 4.0f;
    CHECK_NEAR(policy.EffectiveScale(), 1.5, 0.0);
    CHECK_NEAR(policy.Scale(0.2), 0.3, 1e-6);
    CHECK_NEAR(policy.ScaleSpring(base).stiffness, slow.stiffness, 1e-4);
    policy.durationScale = 0.1f;
    CHECK_NEAR(policy.EffectiveScale(), 0.5, 0.0);
    CHECK_NEAR(policy.ScaleSpring(base).stiffness, base.stiffness * 4.0, 1e-2);
    policy.durationScale = std::numeric_limits<float>::quiet_NaN();
    CHECK_NEAR(policy.EffectiveScale(), 1.0, 0.0);
    CHECK_NEAR(policy.Scale(0.2), 0.2, 1e-9);
    policy.durationScale = 1.5f;
    policy.reduced = true;
    CHECK_NEAR(policy.Scale(0.2), 0.0, 0.0);
    CHECK_EQ(policy.ScaleSpring(base).stiffness, base.stiffness);
}

AETHER_TEST("motion: shifting a spring keeps its velocity and remaining distance") {
    Spring spring(0.0f, Motion().slide);
    spring.SetTarget(10.0f);
    for (int i = 0; i < 5; ++i) spring.Step(1.0 / 60.0);
    const float value = spring.Value();
    const float velocity = spring.Velocity();
    spring.ShiftBy(-25.0f);
    CHECK_NEAR(spring.Value(), value - 25.0f, 1e-4);
    CHECK_NEAR(spring.Target(), -15.0f, 1e-4);
    CHECK_EQ(spring.Velocity(), velocity);
    spring.ShiftBy(std::numeric_limits<float>::infinity());
    CHECK_NEAR(spring.Target(), -15.0f, 1e-4);
    for (int i = 0; i < 600 && !spring.IsSettled(); ++i) spring.Step(1.0 / 60.0);
    CHECK(spring.IsSettled());
    CHECK_NEAR(spring.Value(), -15.0f, 1e-6);
}

AETHER_TEST("motion: the launch bounce is a decaying gravity hop that ends at rest") {
    const auto& motion = Motion();
    const float height = 12.0f;
    CHECK_NEAR(LaunchBounceDuration(), motion.launchHop * (1.0 + motion.launchRestitution), 1e-9);
    CHECK_EQ(LaunchBounce(0.0, height), 0.0f);
    CHECK_EQ(LaunchBounce(-0.1, height), 0.0f);
    CHECK_NEAR(LaunchBounce(motion.launchHop / 2, height), height, 1e-4);
    CHECK_NEAR(LaunchBounce(motion.launchHop, height), 0.0, 1e-4);
    const double rebound = motion.launchHop * motion.launchRestitution;
    CHECK_NEAR(LaunchBounce(motion.launchHop + rebound / 2, height),
               height * motion.launchRestitution * motion.launchRestitution, 1e-4);
    CHECK_EQ(LaunchBounce(LaunchBounceDuration(), height), 0.0f);
    CHECK_EQ(LaunchBounce(LaunchBounceDuration() + 1.0, height), 0.0f);
    float previous = 0.0f;
    for (double t = 0.0; t <= LaunchBounceDuration() + 0.05; t += 0.001) {
        const float offset = LaunchBounce(t, height);
        CHECK(offset >= 0.0f);
        CHECK(offset <= height + 1e-4f);
        CHECK(std::abs(offset - previous) < 0.5f);  // continuous at 1 ms steps, including hop boundaries
        previous = offset;
    }
    CHECK_EQ(LaunchBounce(0.1, 0.0f), 0.0f);
    CHECK_EQ(LaunchBounce(0.1, -5.0f), 0.0f);
    CHECK_EQ(LaunchBounce(0.1, std::numeric_limits<float>::quiet_NaN()), 0.0f);
    CHECK_EQ(LaunchBounce(std::numeric_limits<double>::quiet_NaN(), height), 0.0f);
}

AETHER_TEST("motion: the maximum bounce height scales with the icon and bounds every bounce") {
    CHECK_NEAR(MaxBounceHeight(40.0f), 12.0, 1e-5);
    CHECK_NEAR(MaxBounceHeight(32.0f), 9.6, 1e-5);
    CHECK_NEAR(MaxBounceHeight(48.0f), 14.4, 1e-5);
    CHECK_NEAR(MaxBounceHeight(96.0f), Motion().launchMaxHeight, 0.0);
    CHECK_EQ(MaxBounceHeight(0.0f), 0.0f);
    CHECK_EQ(MaxBounceHeight(-40.0f), 0.0f);
    CHECK_EQ(MaxBounceHeight(std::numeric_limits<float>::infinity()), 0.0f);
    float previous = 0.0f;
    for (float size = 32.0f; size <= 96.0f; size += 1.0f) {
        const float maximum = MaxBounceHeight(size);
        CHECK(maximum >= previous);
        previous = maximum;
        for (double t = 0.0; t <= LaunchBounceDuration(); t += 0.01) CHECK(LaunchBounce(t, maximum) <= maximum + 1e-4f);
    }
}
