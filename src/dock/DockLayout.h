// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <d2d1.h>

#include <optional>
#include <span>
#include <vector>

namespace aether::dock {

// Default ambient shadow reaches 3 * 10 + 1 = 31 DIPs; round up on the 4-DIP grid.
inline constexpr float kDockShadowMargin = 32.0f;
inline constexpr float kDockEntranceOffset = 24.0f;

struct DockItemGeometry { D2D1_RECT_F icon{}; float scale = 1; };
struct DockLayout {
    D2D1_RECT_F panel{};
    float windowWidth = 0;
    float windowHeight = 0;
    std::vector<DockItemGeometry> items;
    std::optional<std::size_t> raisedItem; // captured/drop-settling icon is painted and hit-tested last
};
struct DockPlacement {
    RECT windowPx{};
    LONG reservationHeightPx = 0;
};

Result<std::vector<float>> DockMagnification(std::size_t count, float iconSize, float spacing,
                                            float magnification, float width, std::optional<float> pointerX);
Result<DockLayout> ArrangeDockLayout(float iconSize, float spacing, float magnification, float width,
                                    std::span<const float> scales);
// Pure physical-pixel placement. bottomMargin is the visible panel's only bottom gap; transparent
// trailing window padding does not add a second gap. Optional animation reservation includes the
// animated icon/focus extent and shadow; compact reservation includes the resting panel and gap.
Result<DockPlacement> PlaceDock(const DockLayout& layout, const RECT& anchorAreaPx, UINT dpi,
                                float iconSize, float magnification, float bottomMargin,
                                bool reducedMotion, bool reserveAnimationSpace = true);
std::optional<std::size_t> HitTest(const DockLayout& layout, D2D1_POINT_2F point) noexcept;

}  // namespace aether::dock
