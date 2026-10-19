// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Compositor-driven window transitions: a description of opacity, offset and scale keyframes that
// CompositionSurface::Animate plays on the window's root visual (no UI frame loop), plus the
// design-language presets used by the shell surfaces.
#pragma once

#include "graphics/Animation.h"
#include "graphics/Theme.h"

namespace aether::graphics {

// Offsets and the scale center are DIPs relative to the window; the surface converts them to pixels.
struct Transition {
    float opacityFrom = 1.0f;
    float opacityTo = 1.0f;
    float offsetFromX = 0.0f;
    float offsetFromY = 0.0f;
    float offsetToX = 0.0f;
    float offsetToY = 0.0f;
    float scaleFrom = 1.0f;
    float scaleTo = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    Seconds duration = 0.0;  // 0 (or less, or not finite) applies the end state at once
    CubicBezier curve{};

    [[nodiscard]] bool IsInstant() const noexcept { return !(duration > 0.0) || duration > 10.0; }
};

// Where a surface comes from when it enters, or goes to when it leaves: an offset (for example the
// panel height for the Dock, so it slides in from the screen edge), a scale around a center, all
// combined with a fade.
struct TransitionShape {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scale = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
};

// Enter: from `from` and transparent to the resting state, durationNormal with the decelerate curve.
// Exit: from the resting state to `to` and transparent, durationFast with the accelerate curve.
// Durations follow MotionPolicy::Scale (animation speed); reduced motion and high contrast make them
// 0, so the surface shows or hides at once.
[[nodiscard]] Transition MakeEnterTransition(const TransitionShape& from, const DesignTokens& tokens,
                                             const MotionPolicy& motion) noexcept;
[[nodiscard]] Transition MakeExitTransition(const TransitionShape& to, const DesignTokens& tokens,
                                            const MotionPolicy& motion) noexcept;

}  // namespace aether::graphics
