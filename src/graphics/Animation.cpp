// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/Animation.h"

#include <algorithm>

namespace aether::graphics {

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
    }
    return t;
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
}

float Tween::Value(Seconds now) const noexcept {
    if (duration_ <= 0.0 || now >= start_ + duration_) return to_;
    if (now <= start_) return from_;
    const float t = static_cast<float>((now - start_) / duration_);
    return Lerp(from_, to_, Ease(easing_, t));
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
