// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockLayout.h"
#include "graphics/Animation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace aether::dock {
namespace {
bool Valid(float size, float spacing, float magnification, float width) {
    return std::isfinite(size) && size >= 32 && size <= 96 && std::isfinite(spacing) && spacing >= 0 && spacing <= 32 &&
        std::isfinite(magnification) && magnification >= 1 && magnification <= 2 && std::isfinite(width) && width >= 56;
}
}

Result<std::vector<float>> DockMagnification(std::size_t count, float iconSize, float spacing,
                                            float magnification, float width, std::optional<float> pointerX) {
    try {
        if (!Valid(iconSize, spacing, magnification, width) || count > 10000 || (pointerX && !std::isfinite(*pointerX))) {
            return MakeError(ErrorCode::InvalidArgument, "invalid Dock magnification geometry");
        }
        std::vector<float> scales(count, 1);
        if (!pointerX || *pointerX < 0 || *pointerX > width || magnification == 1) return scales;
        const float baseWidth = static_cast<float>(count) * iconSize + static_cast<float>(count > 0 ? count - 1 : 0) * spacing;
        const float start = (width - baseWidth) / 2;
        const float radius = 2.5f * (iconSize + spacing);
        for (std::size_t index = 0; index < count; ++index) {
            const float center = start + static_cast<float>(index) * (iconSize + spacing) + iconSize / 2;
            const float distance = std::abs(*pointerX - center);
            if (distance < radius) scales[index] += (magnification - 1) * 0.5f *
                (1 + std::cos(std::numbers::pi_v<float> * distance / radius));
        }
        return scales;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock magnification could not be calculated"); }
}

Result<DockLayout> ArrangeDockLayout(float iconSize, float spacing, float magnification, float width,
                                    std::span<const float> scales) {
    try {
        if (!Valid(iconSize, spacing, magnification, width) || scales.size() > 10000) {
            return MakeError(ErrorCode::InvalidArgument, "invalid Dock layout geometry");
        }
        float content = 0;
        for (const auto scale : scales) {
            if (!std::isfinite(scale) || scale < 1 || scale > magnification + 0.001f) return MakeError(ErrorCode::InvalidArgument, "invalid Dock icon scale");
            content += iconSize * scale;
        }
        content += static_cast<float>(scales.empty() ? 0 : scales.size() - 1) * spacing;
        DockLayout layout;
        layout.windowWidth = width;
        // The shadow margin protects both layers. The bottom also contains the
        // entrance slide; the top includes lift, scale, bounce and focus outside the icon geometry.
        const float baselineGap = kDockShadowMargin + kDockEntranceOffset + 18;
        layout.windowHeight = std::ceil(iconSize * magnification + graphics::MaxBounceHeight(iconSize) +
            graphics::Motion().liftOffset + 6 + kDockShadowMargin + baselineGap);
        const float baseline = layout.windowHeight - baselineGap;
        float x = (width - content) / 2;
        layout.panel = {x - 12, baseline - iconSize - kDockPanelTopPaddingDip, x + content + 12, baseline + 18};
        for (const auto scale : scales) {
            const float size = iconSize * scale;
            layout.items.push_back({{x, baseline - size, x + size, baseline}, scale});
            x += size + spacing;
        }
        return layout;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock layout could not be arranged"); }
}

Result<DockPlacement> PlaceDock(const DockLayout& layout, const RECT& area, UINT dpi,
                                float iconSize, float magnification, float bottomMargin,
                                bool reducedMotion, bool reserveAnimationSpace) {
    if (!Valid(iconSize, 0, magnification, layout.windowWidth) || dpi < 48 || dpi > 960 ||
        area.right <= area.left || area.bottom <= area.top ||
        !std::isfinite(layout.windowHeight) || layout.windowHeight <= 0 || layout.windowHeight > 512 ||
        !std::isfinite(layout.panel.top) || !std::isfinite(layout.panel.bottom) ||
        layout.panel.top < 0 || layout.panel.bottom <= layout.panel.top || layout.panel.bottom > layout.windowHeight ||
        !std::isfinite(bottomMargin) || bottomMargin < 0 || bottomMargin > 200) {
        return MakeError(ErrorCode::InvalidArgument, "invalid Dock placement geometry");
    }
    const double scale = static_cast<double>(dpi) / 96;
    const auto availableWidth = static_cast<std::int64_t>(area.right) - area.left;
    // Converting an exact monitor width to float DIPs and back can land a fraction above the
    // original integer at fractional DPI. Permit that subpixel error without growing the HWND.
    if (layout.windowWidth * scale > static_cast<double>(availableWidth) + 0.01 ||
        layout.windowWidth * scale > static_cast<double>(std::numeric_limits<LONG>::max())) {
        return MakeError(ErrorCode::InvalidArgument, "Dock exceeds its anchor width");
    }
    const auto pixels = [scale](double dip) { return static_cast<LONG>(std::ceil(dip * scale)); };
    const LONG width = static_cast<LONG>(std::min<std::int64_t>(pixels(layout.windowWidth), availableWidth));
    const LONG height = pixels(layout.windowHeight);
    const LONG margin = pixels(bottomMargin);
    // The resting panel has transparent trailing padding for shadow and entrance motion. Position
    // its visible edge, rather than the HWND edge, relative to the monitor/work-area boundary.
    const LONG trailing = pixels(layout.windowHeight - layout.panel.bottom);
    const auto left = static_cast<std::int64_t>(area.left) + (availableWidth - width) / 2;
    const auto bottom = static_cast<std::int64_t>(area.bottom) - margin + trailing;
    if (bottom > std::numeric_limits<LONG>::max() || bottom - height < std::numeric_limits<LONG>::min()) {
        return MakeError(ErrorCode::InvalidArgument, "Dock exceeds native coordinate limits");
    }
    DockPlacement placement;
    placement.windowPx = {static_cast<LONG>(left), static_cast<LONG>(bottom - height),
        static_cast<LONG>(left + width), static_cast<LONG>(bottom)};
    const float baseline = layout.panel.bottom - 18;
    constexpr float shadowGuard = kDockShadowMargin;
    constexpr float focusGuard = 6;
    const float bounce = reducedMotion ? 0.0f : graphics::MaxBounceHeight(iconSize);
    // Lift grows the icon equally on both sides; its ambient shadow reaches 3 * 8 + 2 DIPs.
    const float liftScale = reducedMotion ? 1.0f : 1 + (graphics::Motion().liftScale - 1) / 2;
    const float iconGuard = reducedMotion ? focusGuard : 29.0f; // includes shadow bucket rounding
    const float earliest = !reserveAnimationSpace ? layout.panel.top : std::min(layout.panel.top - shadowGuard,
        baseline - iconSize * magnification * liftScale - bounce -
        (reducedMotion ? 0.0f : graphics::Motion().liftOffset) - iconGuard);
    const auto firstDrawnPixel = static_cast<std::int64_t>(placement.windowPx.top) +
        static_cast<LONG>(std::floor(earliest * scale));
    if (firstDrawnPixel < std::numeric_limits<LONG>::min()) {
        return MakeError(ErrorCode::InvalidArgument, "Dock exceeds native coordinate limits");
    }
    placement.reservationHeightPx = static_cast<LONG>(static_cast<std::int64_t>(area.bottom) - firstDrawnPixel);
    return placement;
}

std::optional<std::size_t> HitTest(const DockLayout& layout, D2D1_POINT_2F point) noexcept {
    if (layout.raisedItem && *layout.raisedItem < layout.items.size()) {
        const auto& icon = layout.items[*layout.raisedItem].icon;
        if (point.x >= icon.left && point.x < icon.right && point.y >= icon.top && point.y < icon.bottom)
            return layout.raisedItem;
    }
    // Reordering can briefly cross icon rectangles. Later items are drawn on top, so they own the
    // pointer in a crossing region, matching the visual stack rather than the old slot order.
    for (std::size_t remaining = layout.items.size(); remaining > 0; --remaining) {
        const auto index = remaining - 1;
        const auto& icon = layout.items[index].icon;
        if (point.x >= icon.left && point.x < icon.right && point.y >= icon.top && point.y < icon.bottom) return index;
    }
    return std::nullopt;
}

}  // namespace aether::dock
