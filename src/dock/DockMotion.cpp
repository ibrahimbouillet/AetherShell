// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockMotion.h"

#include <cmath>
#include <numbers>

namespace aether::dock {

std::optional<std::size_t> CrossedDockSlot(std::span<const float> centers,
    std::size_t current, float draggedCenter) noexcept {
    if (current >= centers.size() || !std::isfinite(draggedCenter)) return std::nullopt;
    for (std::size_t i = 0; i < centers.size(); ++i)
        if (!std::isfinite(centers[i]) || (i && centers[i] <= centers[i - 1])) return std::nullopt;
    auto target = current;
    if (draggedCenter > centers[current]) {
        while (target + 1 < centers.size() && draggedCenter >= centers[target + 1]) ++target;
    } else {
        while (target && draggedCenter <= centers[target - 1]) --target;
    }
    return target;
}

DockAttentionVisual DockAttentionAt(double elapsed) noexcept {
    if (!std::isfinite(elapsed) || elapsed <= 0 || elapsed >= kDockAttentionDuration) return {};
    constexpr double cycle = kDockAttentionDuration / 3;
    const double phase = std::fmod(elapsed, cycle) / cycle;
    const double wave = std::sin(std::numbers::pi * phase);
    const auto pulse = static_cast<float>(wave * wave * (1 - elapsed / kDockAttentionDuration));
    return {pulse * 0.65f, pulse * 6.0f};
}

} // namespace aether::dock
