// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockLayout.h"

#include <algorithm>
#include <cmath>
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
        layout.windowHeight = std::ceil(iconSize * magnification + 56);
        const float baseline = layout.windowHeight - 30;
        float x = (width - content) / 2;
        layout.panel = {x - 12, baseline - iconSize - 12, x + content + 12, baseline + 18};
        for (const auto scale : scales) {
            const float size = iconSize * scale;
            layout.items.push_back({{x, baseline - size, x + size, baseline}, scale});
            x += size + spacing;
        }
        return layout;
    } catch (...) { return MakeError(ErrorCode::Internal, "Dock layout could not be arranged"); }
}

std::optional<std::size_t> HitTest(const DockLayout& layout, D2D1_POINT_2F point) noexcept {
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
