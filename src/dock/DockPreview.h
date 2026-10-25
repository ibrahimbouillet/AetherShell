// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Window-preview support for the Dock card (FR-034, T43): card geometry and the identity checks
// behind its actions. Geometry is pure. Identity and close requests use documented USER32 calls and
// never activate, enumerate or terminate anything else.
//
// Identity: a tile captures its window's HWND together with the owning process and thread when it is
// created. An action is accepted only while that same HWND still belongs to the same process and
// thread AND the window tracker still lists it for the represented group, so a stale card can never
// act on a reused handle, a replacement window or a sibling.
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Result.h"

#include <d2d1.h>
#include <windows.h>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace aether::dock {

struct PreviewWindowIdentity {
    HWND handle = nullptr;
    DWORD processId = 0;
    DWORD threadId = 0;
};

[[nodiscard]] std::optional<PreviewWindowIdentity> CaptureWindowIdentity(HWND window) noexcept;
// Pure: the same live window, not merely the same handle value.
[[nodiscard]] bool SameWindowIdentity(const PreviewWindowIdentity& captured,
                                      const PreviewWindowIdentity& current) noexcept;
[[nodiscard]] bool IsTrackedWindow(HWND window, std::span<const apps::RunningWindow> tracked) noexcept;
// Captured identity still live and still tracked for the group.
[[nodiscard]] bool IsCurrentPreviewWindow(const PreviewWindowIdentity& captured,
                                          std::span<const apps::RunningWindow> tracked) noexcept;

// Asks exactly this window to close the normal way (a posted WM_SYSCOMMAND/SC_CLOSE, as the window
// menu's Close does). Ok only means Windows accepted the request: the application can show its own
// save prompt or refuse, and the caller must keep showing the window until the tracker observes its
// destruction. Never terminates a process and never touches other windows of the group.
// NotFound: the identity is stale. AccessDenied: Windows blocked the message (for example UIPI).
[[nodiscard]] Result<void> RequestNormalClose(const PreviewWindowIdentity& identity);

// Card geometry in DIPs. All tiles share one height, so the card is a single landscape row above
// the icon; each tile has a header (icon, title, close button at its upper right) and a thumbnail
// box in which the source keeps its aspect ratio without stretching or cropping.
struct PreviewLayoutMetrics {
    float padding = 6.0f;        // card edge to tiles
    float gap = 4.0f;            // between tiles
    float tilePadding = 8.0f;
    float headerHeight = 28.0f;  // icon + title row; the close button is a square of this size
    float headerGap = 6.0f;
    float iconSize = 16.0f;
    float thumbnailWidth = 240.0f;   // landscape box (16:10), close to the Windows taskbar preview size
    float thumbnailHeight = 150.0f;
    float moreWidth = 44.0f;     // trailing "+N" column when not every window fits
};
struct PreviewTileGeometry {
    D2D1_RECT_F tile{};
    D2D1_RECT_F icon{};
    D2D1_RECT_F title{};
    D2D1_RECT_F close{};
    D2D1_RECT_F thumbnailBox{};
    D2D1_RECT_F thumbnail{};  // aspect-preserving fit, centered in thumbnailBox
};
struct PreviewLayout {
    D2D1_SIZE_F card{};
    std::vector<PreviewTileGeometry> tiles;
    std::optional<D2D1_RECT_F> more;  // "+N" column, when windows were left out
};

// How many tiles fit into `availableWidthDip` (at least one, at most `limit`).
[[nodiscard]] std::size_t PreviewTileCapacity(float availableWidthDip, std::size_t limit,
                                              const PreviewLayoutMetrics& metrics = {}) noexcept;
// `sourcePx` holds each window's thumbnail source size in physical pixels (zero when unknown, which
// shows a box-sized placeholder). A source is never enlarged beyond one source pixel per
// destination pixel at `dpi`. `omitted` > 0 adds the "+N" column.
[[nodiscard]] PreviewLayout LayoutPreviewCard(std::span<const SIZE> sourcePx, UINT dpi, std::size_t omitted = 0,
                                              const PreviewLayoutMetrics& metrics = {});
// Physical pixels inside a DIP rectangle (rounded inward, never empty for a non-empty input).
[[nodiscard]] RECT PreviewPixelsInside(const D2D1_RECT_F& rect, UINT dpi) noexcept;

}  // namespace aether::dock
