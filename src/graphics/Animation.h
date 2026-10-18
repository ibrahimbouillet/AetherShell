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
// Time is expressed in seconds (double) from FrameScheduler::Now().
// MotionPolicy::reduced makes every animation complete instantly.
#pragma once

#include <cmath>

namespace aether::graphics {

using Seconds = double;

enum class Easing { Linear, OutCubic, InOutCubic, OutQuint, OutBack };

[[nodiscard]] float Ease(Easing easing, float t) noexcept;

struct MotionPolicy {
    bool reduced = false;        // user or system asked for reduced motion
    float durationScale = 1.0f;  // "animation intensity": 0.5 faster .. 1.5 slower
    [[nodiscard]] Seconds Scale(Seconds duration) const noexcept {
        return reduced ? 0.0 : duration * static_cast<double>(durationScale);
    }
};

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

[[nodiscard]] inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

}  // namespace aether::graphics
