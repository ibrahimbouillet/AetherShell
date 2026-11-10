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

float DockRubberBand(float distance, float reach) noexcept {
    if (!std::isfinite(distance) || !std::isfinite(reach) || distance <= 0 || reach <= 0) return 0;
    return reach * distance / (distance + reach);
}

DockPull PullDockIcon(float wantedCenter, float low, float high, float reach) noexcept {
    DockPull pull;
    if (!std::isfinite(low) || !std::isfinite(high)) { pull.center = std::isfinite(wantedCenter) ? wantedCenter : 0; return pull; }
    if (high < low) high = low = (low + high) / 2;   // a panel narrower than the icon: its middle
    if (!std::isfinite(wantedCenter)) { pull.center = std::isfinite(low) ? low : 0; return pull; }
    if (wantedCenter > high) {
        pull.stretchRight = DockRubberBand(wantedCenter - high, reach);
        pull.center = high + pull.stretchRight;
    } else if (wantedCenter < low) {
        pull.stretchLeft = DockRubberBand(low - wantedCenter, reach);
        pull.center = low - pull.stretchLeft;
    } else {
        pull.center = wantedCenter;
    }
    return pull;
}

DockAttentionVisual DockAttentionAt(double elapsed) noexcept {
    if (!std::isfinite(elapsed) || elapsed <= 0 || elapsed >= kDockAttentionDuration) return {};
    constexpr double cycle = kDockAttentionDuration / 3;
    const double phase = std::fmod(elapsed, cycle) / cycle;
    const double wave = std::sin(std::numbers::pi * phase);
    const auto pulse = static_cast<float>(wave * wave * (1 - elapsed / kDockAttentionDuration));
    return {pulse * 0.65f, pulse * 6.0f};
}

void DockIndicatorMotion::Retarget(bool running, bool foreground, bool emphasized,
    const graphics::MotionPolicy& policy) noexcept {
    if (!running) {
        // A closed pin must not keep a fading marker or acquire one from hover alone.
        running_ = false;
        width_.SnapTo(0);
        return;
    }
    if (!running_) width_.SnapTo(kDockMarkerDotWidth);
    running_ = true;
    width_.SetParameters(policy.ScaleSpring(graphics::Motion().marker));
    // An already expanded hover marker keeps its target on an observed activation: no restart.
    const float target = foreground || emphasized ? kDockMarkerPillWidth : kDockMarkerDotWidth;
    if (policy.reduced) width_.SnapTo(target);
    else width_.SetTarget(target);
}

} // namespace aether::dock
