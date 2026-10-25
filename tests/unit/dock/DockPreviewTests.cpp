// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "dock/DockPreview.h"

#include <cmath>
#include <cstdint>
#include <limits>

using namespace aether;
using namespace aether::dock;

namespace {
float Width(const D2D1_RECT_F& rect) { return rect.right - rect.left; }
float Height(const D2D1_RECT_F& rect) { return rect.bottom - rect.top; }
bool Inside(const D2D1_RECT_F& inner, const D2D1_RECT_F& outer) {
    return inner.left >= outer.left - 0.001f && inner.top >= outer.top - 0.001f &&
           inner.right <= outer.right + 0.001f && inner.bottom <= outer.bottom + 0.001f;
}
}  // namespace

AETHER_TEST("dock preview layout: thumbnails keep the source aspect ratio inside a compact card") {
    const std::vector<SIZE> sources{{1920, 1080}, {1024, 768}, {900, 1600}, {3440, 1000}};
    for (const SIZE& source : sources) {
        const auto layout = LayoutPreviewCard(std::span(&source, 1), 96);
        REQUIRE_EQ(layout.tiles.size(), 1u);
        const auto& tile = layout.tiles[0];
        CHECK(Inside(tile.thumbnail, tile.thumbnailBox));
        CHECK(Inside(tile.thumbnailBox, tile.tile));
        CHECK_NEAR(Width(tile.thumbnail) / Height(tile.thumbnail), static_cast<float>(source.cx) / static_cast<float>(source.cy), 0.01f);
        // Fitted, not cropped: one dimension touches the box.
        CHECK(std::abs(Width(tile.thumbnail) - Width(tile.thumbnailBox)) < 0.01f ||
              std::abs(Height(tile.thumbnail) - Height(tile.thumbnailBox)) < 0.01f);
        CHECK_NEAR((tile.thumbnail.left + tile.thumbnail.right) / 2, (tile.thumbnailBox.left + tile.thumbnailBox.right) / 2, 0.01f);
        CHECK(Width(tile.title) >= 96.0f);  // narrow sources still leave a readable header
        CHECK(Inside(tile.close, tile.tile));
        CHECK_NEAR(tile.close.right, tile.tile.right - PreviewLayoutMetrics{}.tilePadding, 0.001f);  // upper right
        CHECK_NEAR(tile.close.top, tile.tile.top + PreviewLayoutMetrics{}.tilePadding, 0.001f);
        CHECK(Width(tile.close) >= 24.0f);  // a clearly usable target
        CHECK(tile.title.right <= tile.close.left);
    }
}

AETHER_TEST("dock preview layout: compact gutters preserve the wide thumbnail at every DPI") {
    const SIZE source{2560, 1152};
    for (const UINT dpi : {96u, 144u, 192u}) {
        const auto layout = LayoutPreviewCard(std::span(&source, 1), dpi);
        REQUIRE_EQ(layout.tiles.size(), 1u);
        const auto& tile = layout.tiles[0];
        CHECK_NEAR(Width(tile.thumbnail), 240.0f, 0.01f);
        CHECK_NEAR(Height(tile.thumbnail), 108.0f, 0.01f);
        CHECK_NEAR(tile.thumbnailBox.top, tile.thumbnail.top, 0.01f);
        CHECK_NEAR(tile.thumbnailBox.bottom, tile.thumbnail.bottom, 0.01f);
        CHECK(tile.thumbnail.left <= 8.0f);
        CHECK(layout.card.width - tile.thumbnail.right <= 8.0f);
        CHECK(tile.thumbnail.top - tile.close.bottom <= 4.0f);
        CHECK(layout.card.height - tile.thumbnail.bottom <= 8.0f);
        CHECK(Width(tile.close) >= 28.0f);
    }
}

AETHER_TEST("dock preview layout: small windows are not enlarged beyond their physical pixels") {
    const SIZE tinyWindow{200, 120};
    const auto at96 = LayoutPreviewCard(std::span(&tinyWindow, 1), 96);
    CHECK_NEAR(Width(at96.tiles[0].thumbnail), 200.0f, 0.01f);
    CHECK_NEAR(Height(at96.tiles[0].thumbnail), 120.0f, 0.01f);
    const auto at192 = LayoutPreviewCard(std::span(&tinyWindow, 1), 192);
    CHECK_NEAR(Width(at192.tiles[0].thumbnail), 100.0f, 0.01f);  // 200 px at 200 % is 100 DIPs
    const SIZE unknown{0, 0};
    const auto placeholder = LayoutPreviewCard(std::span(&unknown, 1), 96);
    CHECK_NEAR(Width(placeholder.tiles[0].thumbnail), Width(placeholder.tiles[0].thumbnailBox), 0.01f);
}

AETHER_TEST("dock preview layout: several windows form one ordered row with an honest overflow column") {
    const std::vector<SIZE> sources{{1600, 1000}, {800, 1200}, {1280, 720}};
    const auto layout = LayoutPreviewCard(sources, 144, 2);
    REQUIRE_EQ(layout.tiles.size(), 3u);
    for (std::size_t index = 1; index < layout.tiles.size(); ++index) {
        CHECK(layout.tiles[index].tile.left >= layout.tiles[index - 1].tile.right);
        CHECK_NEAR(Height(layout.tiles[index].tile), Height(layout.tiles[0].tile), 0.001f);
        CHECK(layout.tiles[index].close.left > layout.tiles[index - 1].close.right);  // every X is distinct
    }
    REQUIRE(layout.more.has_value());
    CHECK(layout.more->left >= layout.tiles.back().tile.right);
    CHECK_NEAR(layout.card.width, layout.more->right + PreviewLayoutMetrics{}.padding, 0.001f);
    CHECK_FALSE(LayoutPreviewCard(sources, 144).more.has_value());
}

AETHER_TEST("dock preview layout: capacity follows the monitor width and stays within bounds") {
    CHECK_EQ(PreviewTileCapacity(100.0f, 6), 1u);
    CHECK_EQ(PreviewTileCapacity(std::numeric_limits<float>::quiet_NaN(), 6), 1u);
    const auto wide = PreviewTileCapacity(1280.0f, 6);
    CHECK(wide >= 3u); CHECK(wide <= 6u);
    CHECK_EQ(PreviewTileCapacity(100000.0f, 6), 6u);
    CHECK_EQ(PreviewTileCapacity(100000.0f, 0), 1u);
}

AETHER_TEST("dock preview layout: destination pixels stay inside the DIP rectangle at every DPI") {
    const D2D1_RECT_F rect{10.3f, 20.6f, 138.7f, 100.2f};
    for (UINT dpi : {96u, 120u, 144u, 192u}) {
        const RECT pixels = PreviewPixelsInside(rect, dpi);
        const float scale = static_cast<float>(dpi) / 96.0f;
        CHECK(static_cast<float>(pixels.left) >= rect.left * scale - 0.02f);
        CHECK(static_cast<float>(pixels.top) >= rect.top * scale - 0.02f);
        CHECK(static_cast<float>(pixels.right) <= rect.right * scale + 0.02f);
        CHECK(static_cast<float>(pixels.bottom) <= rect.bottom * scale + 0.02f);
        CHECK(pixels.right > pixels.left); CHECK(pixels.bottom > pixels.top);
    }
    const RECT tiny = PreviewPixelsInside({5.2f, 5.2f, 5.4f, 5.4f}, 96);
    CHECK(tiny.right > tiny.left); CHECK(tiny.bottom > tiny.top);
}

AETHER_TEST("dock preview identity: a reused handle or an untracked sibling is never the same window") {
    const auto handle = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1234));
    const PreviewWindowIdentity captured{handle, 40, 41};
    CHECK(SameWindowIdentity(captured, {handle, 40, 41}));
    CHECK_FALSE(SameWindowIdentity(captured, {handle, 40, 99}));  // same value, another thread
    CHECK_FALSE(SameWindowIdentity(captured, {handle, 77, 41}));  // same value, another process
    CHECK_FALSE(SameWindowIdentity(captured, {reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1238)), 40, 41}));
    CHECK_FALSE(SameWindowIdentity({handle, 0, 0}, {handle, 0, 0}));
    CHECK_FALSE(SameWindowIdentity({nullptr, 40, 41}, {nullptr, 40, 41}));
    const std::vector<apps::RunningWindow> tracked{{reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x2000)), L"Sibling", false}};
    CHECK_FALSE(IsTrackedWindow(handle, tracked));
    CHECK(IsTrackedWindow(tracked[0].handle, tracked));
    CHECK_FALSE(IsTrackedWindow(nullptr, tracked));
    CHECK_FALSE(IsCurrentPreviewWindow(captured, tracked));  // not tracked for the group any longer
    CHECK_FALSE(CaptureWindowIdentity(nullptr).has_value());
    const auto stale = RequestNormalClose(PreviewWindowIdentity{handle, 40, 41});
    REQUIRE_FALSE(stale.ok());
    CHECK_EQ(stale.error().code, ErrorCode::NotFound);  // nothing is posted for a dead identity
}
