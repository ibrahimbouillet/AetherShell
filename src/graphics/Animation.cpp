// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/Animation.h"

#include <algorithm>

namespace aether::graphics {
namespace {

// One coordinate of a cubic Bezier whose end points are 0 and 1, at parameter u.
double BezierCoordinate(double a1, double a2, double u) noexcept {
    const double v = 1.0 - u;
    return 3.0 * v * v * u * a1 + 3.0 * v * u * u * a2 + u * u * u;
}
double BezierSlope(double a1, double a2, double u) noexcept {
    const double v = 1.0 - u;
    return 3.0 * v * v * a1 + 6.0 * v * u * (a2 - a1) + 3.0 * u * u * (1.0 - a2);
}
double FiniteOr(float value, double fallback) noexcept {
    return std::isfinite(value) ? static_cast<double>(value) : fallback;
}

MotionTable MakeMotionTable() noexcept {
    MotionTable table;
    table.magnify = Spring::Parameters{};  // 420 / 38
    table.press = SpringFromDampingRatio(900.0f, 0.75f);
    table.lift = SpringFromDampingRatio(600.0f, 0.8f);
    table.slide = SpringFromDampingRatio(380.0f, 0.85f);
    return table;
}

}  // namespace

float Ease(Easing easing, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
        case Easing::Linear: return t;
        case Easing::OutCubic: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case Easing::InOutCubic:
            if (t < 0.5f) return 4.0f * t * t * t;
            {
                const float u = -2.0f * t + 2.0f;
                return 1.0f - u * u * u / 2.0f;
            }
        case Easing::OutQuint: {
            const float u = 1.0f - t;
            return 1.0f - u * u * u * u * u;
        }
        case Easing::OutBack: {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            const float u = t - 1.0f;
            return 1.0f + c3 * u * u * u + c1 * u * u;
        }
        case Easing::Standard: return EvaluateCubicBezier(CubicBezier{0.2f, 0.0f, 0.0f, 1.0f}, t);
        case Easing::Decelerate: return EvaluateCubicBezier(CubicBezier{0.0f, 0.0f, 0.0f, 1.0f}, t);
        case Easing::Accelerate: return EvaluateCubicBezier(CubicBezier{0.3f, 0.0f, 1.0f, 1.0f}, t);
    }
    return t;
}

float EvaluateCubicBezier(const CubicBezier& curve, float t) noexcept {
    if (!(t > 0.0f)) return 0.0f;  // also maps NaN to the start
    if (t >= 1.0f) return 1.0f;
    const double x1 = std::clamp(FiniteOr(curve.x1, 0.0), 0.0, 1.0);
    const double x2 = std::clamp(FiniteOr(curve.x2, 1.0), 0.0, 1.0);
    const double y1 = FiniteOr(curve.y1, x1);
    const double y2 = FiniteOr(curve.y2, x2);
    const double target = t;
    constexpr double kTolerance = 1e-9;
    // Newton converges in a few steps for the usual curves; flat or escaping steps fall back below.
    double u = target;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const double error = BezierCoordinate(x1, x2, u) - target;
        if (std::fabs(error) < kTolerance) return static_cast<float>(BezierCoordinate(y1, y2, u));
        const double slope = BezierSlope(x1, x2, u);
        if (std::fabs(slope) < 1e-9) break;
        const double next = u - error / slope;
        if (next < 0.0 || next > 1.0) break;
        u = next;
    }
    // x(u) is monotonic for x1, x2 in 0..1, so bisection always converges.
    double low = 0.0;
    double high = 1.0;
    u = target;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double x = BezierCoordinate(x1, x2, u);
        if (std::fabs(x - target) < kTolerance) break;
        if (x < target) low = u;
        else high = u;
        u = (low + high) / 2.0;
    }
    return static_cast<float>(BezierCoordinate(y1, y2, u));
}

Spring::Parameters SpringFromDampingRatio(float stiffness, float dampingRatio, float mass) noexcept {
    Spring::Parameters parameters;
    if (std::isfinite(stiffness) && stiffness > 0.0f) parameters.stiffness = stiffness;
    if (std::isfinite(mass) && mass > 0.0f) parameters.mass = mass;
    const float ratio = std::isfinite(dampingRatio) ? std::clamp(dampingRatio, 0.05f, 10.0f) : 1.0f;
    parameters.damping = 2.0f * ratio * std::sqrt(parameters.stiffness * parameters.mass);
    return parameters;
}

Spring::Parameters MotionPolicy::ScaleSpring(Spring::Parameters parameters) const noexcept {
    if (reduced) return parameters;
    const float scale = EffectiveScale();
    parameters.stiffness /= scale * scale;
    parameters.damping /= scale;
    parameters.restVelocity /= scale;
    return parameters;
}

const MotionTable& Motion() noexcept {
    static const MotionTable table = MakeMotionTable();
    return table;
}

Seconds LaunchBounceDuration() noexcept {
    const auto& motion = Motion();
    Seconds total = 0.0;
    Seconds hop = motion.launchHop;
    for (int index = 0; index <= motion.launchRebounds; ++index) {
        total += hop;
        hop *= static_cast<double>(motion.launchRestitution);
    }
    return total;
}

float LaunchBounce(Seconds elapsed, float height) noexcept {
    if (!std::isfinite(elapsed) || elapsed <= 0.0 || !std::isfinite(height) || height <= 0.0f) return 0.0f;
    const auto& motion = Motion();
    // Each hop is a parabola: offset = peak * 4 p (1 - p) over its duration. A rebound with
    // restitution r lasts r times as long and peaks r^2 times as high, as under constant gravity.
    Seconds start = 0.0;
    Seconds hop = motion.launchHop;
    double peak = static_cast<double>(height);
    for (int index = 0; index <= motion.launchRebounds; ++index) {
        if (elapsed < start + hop) {
            const double progress = (elapsed - start) / hop;
            return static_cast<float>(std::max(0.0, peak * 4.0 * progress * (1.0 - progress)));
        }
        start += hop;
        hop *= static_cast<double>(motion.launchRestitution);
        peak *= static_cast<double>(motion.launchRestitution) * static_cast<double>(motion.launchRestitution);
    }
    return 0.0f;
}

float MaxBounceHeight(float iconSize) noexcept {
    if (!std::isfinite(iconSize) || iconSize <= 0.0f) return 0.0f;
    const auto& motion = Motion();
    return std::min(iconSize * motion.launchHeightFactor, motion.launchMaxHeight);
}

void Tween::SnapTo(float value) noexcept {
    from_ = to_ = value;
    duration_ = 0.0;
}

void Tween::AnimateTo(float target, Seconds now, Seconds duration, Easing easing) noexcept {
    if (duration <= 0.0) {
        SnapTo(target);
        return;
    }
    if (target == to_ && IsAnimating(now)) return;  // already heading there; keep the curve smooth
    from_ = Value(now);
    to_ = target;
    start_ = now;
    duration_ = duration;
    easing_ = easing;
    useCurve_ = false;
}

void Tween::AnimateTo(float target, Seconds now, Seconds duration, const CubicBezier& curve) noexcept {
    if (duration <= 0.0) {
        SnapTo(target);
        return;
    }
    if (target == to_ && IsAnimating(now)) return;
    from_ = Value(now);
    to_ = target;
    start_ = now;
    duration_ = duration;
    curve_ = curve;
    useCurve_ = true;
}

float Tween::Value(Seconds now) const noexcept {
    if (duration_ <= 0.0 || now >= start_ + duration_) return to_;
    if (now <= start_) return from_;
    const float t = static_cast<float>((now - start_) / duration_);
    return Lerp(from_, to_, useCurve_ ? EvaluateCubicBezier(curve_, t) : Ease(easing_, t));
}

void Spring::Step(Seconds dt) noexcept {
    if (IsSettled()) {
        value_ = target_;
        velocity_ = 0.0f;
        return;
    }
    dt = std::clamp(dt, 0.0, 0.1);
    // Semi-implicit Euler with fixed 2 ms sub-steps: deterministic for any frame rate.
    constexpr double kStep = 0.002;
    double remaining = dt;
    while (remaining > 0.0) {
        const float h = static_cast<float>(std::min(kStep, remaining));
        const float displacement = value_ - target_;
        const float force = -parameters_.stiffness * displacement - parameters_.damping * velocity_;
        velocity_ += (force / parameters_.mass) * h;
        value_ += velocity_ * h;
        remaining -= kStep;
    }
    if (IsSettled()) {
        value_ = target_;
        velocity_ = 0.0f;
    }
}

}  // namespace aether::graphics
