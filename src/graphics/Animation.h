// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Frame-rate independent, interruptible animation primitives.
//
// * Tween: time-based interpolation with an easing curve. Retargeting while
//   running starts from the current value, so rapid pointer movement never
//   causes jumps.
// * Spring: damped harmonic oscillator integrated with fixed sub-steps.
//   Naturally interruptible (it keeps its velocity) and stable at any frame
//   rate; used for hover magnification.
//
// * CubicBezier: CSS-style timing curves for tweens and composition animations.
// * MotionTable: the one shared set of spring presets and gesture constants used by the
//   Dock and the Top Bar, plus the launch bounce.
//
// Time is expressed in seconds (double) from FrameScheduler::Now().
// MotionPolicy::reduced makes every animation complete instantly.
#pragma once

#include <cmath>

namespace aether::graphics {

using Seconds = double;

enum class Easing { Linear, OutCubic, InOutCubic, OutQuint, OutBack };

[[nodiscard]] float Ease(Easing easing, float t) noexcept;

// Timing curve through (0,0), (x1,y1), (x2,y2) and (1,1), like CSS cubic-bezier(). y1 and y2 may leave
// 0..1 for overshoot; x1 and x2 are clamped to 0..1 so progress stays a function of time.
struct CubicBezier {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 1.0f;
    float y2 = 1.0f;
};

// Progress of `curve` at time fraction `t` (clamped to 0..1; exactly 0 and 1 at the ends). Newton
// iteration with a bisection fallback keeps the error below 1e-5. Non-finite control values fall back
// to the linear curve's.
[[nodiscard]] float EvaluateCubicBezier(const CubicBezier& curve, float t) noexcept;

class Tween {
public:
    explicit Tween(float value = 0.0f) noexcept : from_(value), to_(value) {}

    void SnapTo(float value) noexcept;
    // Starts animating from the value at `now` towards `target`. A zero or negative duration snaps.
    void AnimateTo(float target, Seconds now, Seconds duration, Easing easing = Easing::OutCubic) noexcept;

    [[nodiscard]] float Value(Seconds now) const noexcept;
    [[nodiscard]] float Target() const noexcept { return to_; }
    [[nodiscard]] bool IsAnimating(Seconds now) const noexcept { return duration_ > 0.0 && now < start_ + duration_; }

private:
    float from_;
    float to_;
    Seconds start_ = 0.0;
    Seconds duration_ = 0.0;
    Easing easing_ = Easing::Linear;
};

class Spring {
public:
    struct Parameters {
        float stiffness = 420.0f;  // N/m
        float damping = 38.0f;     // N*s/m; critical damping is 2*sqrt(stiffness*mass)
        float mass = 1.0f;
        float restDelta = 0.0005f;     // settle when |target - value| and |velocity| are below these
        float restVelocity = 0.005f;
    };

    explicit Spring(float value = 0.0f, Parameters parameters = Parameters()) noexcept
        : value_(value), target_(value), parameters_(parameters) {}

    void SnapTo(float value) noexcept {
        value_ = target_ = value;
        velocity_ = 0.0f;
    }
    void SetTarget(float target) noexcept { target_ = target; }
    void SetParameters(const Parameters& parameters) noexcept { parameters_ = parameters; }
    // Moves value and target together and keeps the velocity, so re-basing a coordinate system
    // mid-flight (a relayout under a moving icon) keeps the motion's momentum. Non-finite deltas are ignored.
    void ShiftBy(float delta) noexcept {
        if (!std::isfinite(delta)) return;
        value_ += delta;
        target_ += delta;
    }

    // Advances by `dt` seconds (clamped to 100 ms so a stalled frame cannot explode the simulation).
    void Step(Seconds dt) noexcept;

    [[nodiscard]] float Value() const noexcept { return value_; }
    [[nodiscard]] float Target() const noexcept { return target_; }
    [[nodiscard]] float Velocity() const noexcept { return velocity_; }
    [[nodiscard]] bool IsSettled() const noexcept {
        return std::fabs(target_ - value_) < parameters_.restDelta && std::fabs(velocity_) < parameters_.restVelocity;
    }

private:
    float value_;
    float target_;
    float velocity_ = 0.0f;
    Parameters parameters_;
};

// Spring parameters from a stiffness and a damping ratio: damping = 2 * ratio * sqrt(stiffness * mass).
// A ratio below 1 overshoots slightly, 1 is critically damped. Invalid stiffness or mass use the
// defaults; the ratio is clamped to 0.05..10 so every spring settles.
[[nodiscard]] Spring::Parameters SpringFromDampingRatio(float stiffness, float dampingRatio, float mass = 1.0f) noexcept;

struct MotionPolicy {
    bool reduced = false;        // user or system asked for reduced motion
    float durationScale = 1.0f;  // "animation intensity": 0.5 faster .. 1.5 slower

    // durationScale clamped to 0.5..1.5; 1 when it is not finite.
    [[nodiscard]] float EffectiveScale() const noexcept {
        if (!std::isfinite(durationScale)) return 1.0f;
        return durationScale < 0.5f ? 0.5f : durationScale > 1.5f ? 1.5f : durationScale;
    }
    [[nodiscard]] Seconds Scale(Seconds duration) const noexcept {
        return reduced ? 0.0 : duration * static_cast<double>(EffectiveScale());
    }
    // The same spring running EffectiveScale() times slower (stiffness / s^2, damping / s, rest
    // velocity / s), so springs follow the animation-speed setting like tweens. Reduced motion
    // returns the parameters unchanged: callers snap springs instead of stepping them.
    [[nodiscard]] Spring::Parameters ScaleSpring(Spring::Parameters parameters) const noexcept;
};

// The one shared motion table. The Dock and the Top Bar read these values
// instead of keeping local constants. Distances are DIPs, times seconds before MotionPolicy scaling.
struct MotionTable {
    Spring::Parameters magnify;   // hover magnification: stiffness 420, damping 38 (unchanged since 0.1)
    Spring::Parameters press;     // press feedback: stiffness 900, damping ratio 0.75
    Spring::Parameters lift;      // drag lift: stiffness 600, damping ratio 0.8
    Spring::Parameters slide;     // neighbours making room and drop settle: stiffness 380, damping ratio 0.85
    Seconds longPress = 0.350;    // a press held this long lifts the icon for dragging
    float liftOffset = 8.0f;      // a lifted icon rises by this many DIPs...
    float liftScale = 1.08f;      // ...and grows by this factor
    float pressScale = 0.92f;     // a pressed icon shrinks to this factor
    // Launch bounce: a gravity hop whose rebounds keep `launchRestitution` of the vertical speed.
    Seconds launchHop = 0.36;     // duration of the first hop
    float launchRestitution = 0.45f;
    int launchRebounds = 1;       // rebounds after the first hop
    float launchHeightFactor = 0.3f;  // peak height relative to the icon size...
    float launchMaxHeight = 24.0f;    // ...capped in DIPs
};
[[nodiscard]] const MotionTable& Motion() noexcept;

// Upward offset in DIPs (never negative) of a launching icon `elapsed` seconds after the launch
// started, for a first hop that peaks at `height`. 0 before the start, after LaunchBounceDuration()
// and for invalid input. Reduced motion is the caller's decision (it skips the bounce).
[[nodiscard]] float LaunchBounce(Seconds elapsed, float height) noexcept;
[[nodiscard]] Seconds LaunchBounceDuration() noexcept;
// Peak launch-bounce height for an icon size: the single value used for drawing, the window extent
// and (with dock.reserveAnimationSpace) the AppBar reservation. 0 for an invalid size.
[[nodiscard]] float MaxBounceHeight(float iconSize) noexcept;

[[nodiscard]] inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

}  // namespace aether::graphics
