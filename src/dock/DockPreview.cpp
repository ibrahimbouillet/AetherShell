// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockPreview.h"

#include "platform/windows/Win32Error.h"

#include <algorithm>
#include <cmath>

namespace aether::dock {

std::optional<PreviewWindowIdentity> CaptureWindowIdentity(HWND window) noexcept {
    if (!window || !::IsWindow(window)) return std::nullopt;
    PreviewWindowIdentity identity;
    identity.handle = window;
    identity.threadId = ::GetWindowThreadProcessId(window, &identity.processId);
    if (!identity.threadId || !identity.processId) return std::nullopt;
    return identity;
}

bool SameWindowIdentity(const PreviewWindowIdentity& captured, const PreviewWindowIdentity& current) noexcept {
    return captured.handle && captured.handle == current.handle && captured.processId != 0 &&
           captured.processId == current.processId && captured.threadId != 0 && captured.threadId == current.threadId;
}

bool IsTrackedWindow(HWND window, std::span<const apps::RunningWindow> tracked) noexcept {
    return window && std::any_of(tracked.begin(), tracked.end(), [&](const auto& entry) { return entry.handle == window; });
}

bool IsCurrentPreviewWindow(const PreviewWindowIdentity& captured, std::span<const apps::RunningWindow> tracked) noexcept {
    if (!IsTrackedWindow(captured.handle, tracked)) return false;
    const auto current = CaptureWindowIdentity(captured.handle);
    return current && SameWindowIdentity(captured, *current);
}

Result<void> RequestNormalClose(const PreviewWindowIdentity& identity) {
    const auto current = CaptureWindowIdentity(identity.handle);
    if (!current || !SameWindowIdentity(identity, *current))
        return MakeError(ErrorCode::NotFound, "This window has already closed.");
    // The click was this process's last input, so it may let the target bring its own save or
    // confirmation prompt forward. This does not activate anything by itself.
    (void)::AllowSetForegroundWindow(identity.processId);
    if (!::PostMessageW(identity.handle, WM_SYSCOMMAND, SC_CLOSE, 0)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_ACCESS_DENIED) return MakeError(ErrorCode::AccessDenied, "Windows did not allow closing this window.");
        return platform::ErrorFromWin32(error, "requesting a window to close");
    }
    return Ok();
}

std::size_t PreviewTileCapacity(float available, std::size_t limit, const PreviewLayoutMetrics& m) noexcept {
    limit = std::max<std::size_t>(1, limit);
    if (!std::isfinite(available)) return 1;
    const float tile = m.thumbnailWidth + 2 * m.tilePadding;
    const float usable = available - 2 * m.padding - m.moreWidth - m.gap;
    if (usable <= tile) return 1;
    const auto count = static_cast<std::size_t>(std::floor((usable + m.gap) / (tile + m.gap)));
    return std::clamp<std::size_t>(count, 1, limit);
}

PreviewLayout LayoutPreviewCard(std::span<const SIZE> sources, UINT dpi, std::size_t omitted, const PreviewLayoutMetrics& m) {
    PreviewLayout layout;
    const float scale = dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
    const float tileWidth = m.thumbnailWidth + 2 * m.tilePadding;
    const float tileHeight = 2 * m.tilePadding + m.headerHeight + m.headerGap + m.thumbnailHeight;
    float x = m.padding;
    for (const SIZE& source : sources) {
        PreviewTileGeometry tile;
        tile.tile = {x, m.padding, x + tileWidth, m.padding + tileHeight};
        const float left = x + m.tilePadding, right = x + tileWidth - m.tilePadding, top = m.padding + m.tilePadding;
        tile.close = {right - m.headerHeight, top, right, top + m.headerHeight};
        const float iconTop = top + (m.headerHeight - m.iconSize) / 2;
        tile.icon = {left + 2, iconTop, left + 2 + m.iconSize, iconTop + m.iconSize};
        tile.title = {tile.icon.right + 8, top, tile.close.left - 4, top + m.headerHeight};
        tile.thumbnailBox = {left, top + m.headerHeight + m.headerGap, right, top + m.headerHeight + m.headerGap + m.thumbnailHeight};
        float width = m.thumbnailWidth, height = m.thumbnailHeight;
        if (source.cx > 0 && source.cy > 0) {
            const float sourceWidth = static_cast<float>(source.cx) / scale, sourceHeight = static_cast<float>(source.cy) / scale;
            const float fit = std::min({m.thumbnailWidth / sourceWidth, m.thumbnailHeight / sourceHeight, 1.0f});
            width = std::max(1.0f, sourceWidth * fit);
            height = std::max(1.0f, sourceHeight * fit);
        }
        const float cx = (tile.thumbnailBox.left + tile.thumbnailBox.right) / 2;
        const float cy = (tile.thumbnailBox.top + tile.thumbnailBox.bottom) / 2;
        tile.thumbnail = {cx - width / 2, cy - height / 2, cx + width / 2, cy + height / 2};
        layout.tiles.push_back(tile);
        x += tileWidth + m.gap;
    }
    if (omitted > 0) {
        layout.more = D2D1_RECT_F{x, m.padding, x + m.moreWidth, m.padding + tileHeight};
        x += m.moreWidth + m.gap;
    }
    layout.card = {std::max(x - m.gap + m.padding, 2 * m.padding), tileHeight + 2 * m.padding};
    return layout;
}

RECT PreviewPixelsInside(const D2D1_RECT_F& rect, UINT dpi) noexcept {
    const float scale = dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
    RECT result{static_cast<LONG>(std::ceil(rect.left * scale - 0.01f)), static_cast<LONG>(std::ceil(rect.top * scale - 0.01f)),
        static_cast<LONG>(std::floor(rect.right * scale + 0.01f)), static_cast<LONG>(std::floor(rect.bottom * scale + 0.01f))};
    if (rect.right > rect.left && result.right <= result.left) result.right = result.left + 1;
    if (rect.bottom > rect.top && result.bottom <= result.top) result.bottom = result.top + 1;
    return result;
}

}  // namespace aether::dock
