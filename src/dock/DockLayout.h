// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"

#include <d2d1.h>

#include <optional>
#include <span>
#include <vector>

namespace aether::dock {

struct DockItemGeometry { D2D1_RECT_F icon{}; float scale = 1; };
struct DockLayout {
    D2D1_RECT_F panel{};
    float windowWidth = 0;
    float windowHeight = 0;
    std::vector<DockItemGeometry> items;
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
// trailing window padding does not add a second gap. Reservation includes the maximum animated
// icon/focus extent and a panel-shadow margin, keeping maximized clients above Dock controls.
Result<DockPlacement> PlaceDock(const DockLayout& layout, const RECT& anchorAreaPx, UINT dpi,
                                float iconSize, float magnification, float bottomMargin,
                                bool reducedMotion);
std::optional<std::size_t> HitTest(const DockLayout& layout, D2D1_POINT_2F point) noexcept;

}  // namespace aether::dock
