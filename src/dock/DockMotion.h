// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "graphics/Animation.h"

#include <cstddef>
#include <optional>
#include <span>

namespace aether::dock {

// Stable resting centers, not animated hit rectangles, decide when neighbours make room.
// Invalid geometry has no destination. The caller excludes fixed entries from the centers.
[[nodiscard]] std::optional<std::size_t> CrossedDockSlot(std::span<const float> restingCenters,
    std::size_t current, float draggedCenter) noexcept;

inline constexpr double kDockAttentionDuration = 2.4;
struct DockAttentionVisual { float opacity = 0; float height = 0; };
// Three soft finite pulses per real request. Motion policy and static accessible cues are owned
// by the window; no timer repeats the request after this envelope finishes.
[[nodiscard]] DockAttentionVisual DockAttentionAt(double elapsed) noexcept;

// A background running dot and the expanded marker. A hovered/previewed background application
// expands to exactly the foreground marker's width; only the foreground keeps active styling.
inline constexpr float kDockMarkerDotWidth = 5.0f;
inline constexpr float kDockMarkerPillWidth = 12.0f;

// The marker belongs to an observed running application, never to a pending launch. Retargets
// preserve both width and velocity, using the dedicated graphics::Motion().marker spring. Its
// center follows the existing animated icon geometry.
class DockIndicatorMotion {
public:
    // `emphasized`: the pointer or keyboard is on this application's icon or its preview card.
    void Retarget(bool running, bool foreground, bool emphasized,
        const graphics::MotionPolicy& policy) noexcept;
    void Step(double elapsed) noexcept { width_.Step(elapsed); }
    [[nodiscard]] float Width() const noexcept { return width_.Value(); }
    [[nodiscard]] float Target() const noexcept { return width_.Target(); }
    [[nodiscard]] bool IsAnimating() const noexcept { return !width_.IsSettled(); }

private:
    bool running_ = false;
    graphics::Spring width_{0};
};

} // namespace aether::dock
