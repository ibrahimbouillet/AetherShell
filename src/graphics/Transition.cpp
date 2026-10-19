// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/Transition.h"

#include <cmath>

namespace aether::graphics {
namespace {

float Finite(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }

Seconds Duration(Seconds base, const DesignTokens& tokens, const MotionPolicy& motion) noexcept {
    if (tokens.mode == ThemeMode::HighContrast) return 0.0;
    const Seconds scaled = motion.Scale(base);
    return std::isfinite(scaled) && scaled > 0.0 ? scaled : 0.0;
}

}  // namespace

Transition MakeEnterTransition(const TransitionShape& from, const DesignTokens& tokens, const MotionPolicy& motion) noexcept {
    Transition transition;
    transition.opacityFrom = 0.0f;
    transition.opacityTo = 1.0f;
    transition.offsetFromX = Finite(from.offsetX, 0.0f);
    transition.offsetFromY = Finite(from.offsetY, 0.0f);
    transition.scaleFrom = Finite(from.scale, 1.0f) > 0.0f ? Finite(from.scale, 1.0f) : 1.0f;
    transition.centerX = Finite(from.centerX, 0.0f);
    transition.centerY = Finite(from.centerY, 0.0f);
    transition.duration = Duration(tokens.durationNormal, tokens, motion);
    transition.curve = tokens.curves.decelerate;
    return transition;
}

Transition MakeExitTransition(const TransitionShape& to, const DesignTokens& tokens, const MotionPolicy& motion) noexcept {
    Transition transition;
    transition.opacityFrom = 1.0f;
    transition.opacityTo = 0.0f;
    transition.offsetToX = Finite(to.offsetX, 0.0f);
    transition.offsetToY = Finite(to.offsetY, 0.0f);
    transition.scaleTo = Finite(to.scale, 1.0f) > 0.0f ? Finite(to.scale, 1.0f) : 1.0f;
    transition.centerX = Finite(to.centerX, 0.0f);
    transition.centerY = Finite(to.centerY, 0.0f);
    transition.duration = Duration(tokens.durationFast, tokens, motion);
    transition.curve = tokens.curves.accelerate;
    return transition;
}

}  // namespace aether::graphics
