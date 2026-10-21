// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

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

} // namespace aether::dock
