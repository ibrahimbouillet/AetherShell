// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure rules of the glass menu: panel placement (anchored above/below, flipping, clamping to the
// work area, points), submenu placement, the panel width rule and the keyboard model. No window,
// device or real input is involved.
#include "AetherTest.h"

#include "graphics/Theme.h"
#include "ui/GlassMenu.h"

#include <cstddef>
#include <limits>
#include <span>
#include <vector>

using namespace aether;
using namespace aether::ui;

namespace {

constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);
const RECT kWork{0, 0, 1000, 800};
constexpr int kGap = 6;
constexpr int kMargin = 8;

MenuRequest Anchored(MenuPlacement placement, RECT anchor) {
    MenuRequest request;
    request.placement = placement;
    request.anchor = anchor;
    return request;
}

std::vector<MenuEntry> KeyboardMenu() {
    // 0 Open, 1 (disabled), 2 separator, 3 Pin, 4 submenu, 5 (disabled)
    std::vector<MenuEntry> entries;
    entries.push_back(MenuEntry::Command(1, L"&Open"));
    entries.push_back(MenuEntry::Command(2, L"&Properties", false));
    entries.push_back(MenuEntry::Separator());
    entries.push_back(MenuEntry::Command(3, L"&Pin to Dock"));
    entries.push_back(MenuEntry::Submenu(L"Dock &size", {MenuEntry::Command(10, L"Small")}));
    entries.push_back(MenuEntry::Command(4, L"&Quit", false));
    return entries;
}

}  // namespace

AETHER_TEST("glass menu placement: below an anchor, centred and one gap away") {
    const RECT anchor{100, 10, 140, 40};
    const POINT origin = PlaceMenuPanel(SIZE{200, 300}, Anchored(MenuPlacement::BelowAnchor, anchor), kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 20L);  // 120 - 100
    CHECK_EQ(origin.y, 46L);  // 40 + 6
}

AETHER_TEST("glass menu placement: above an anchor below the work area (Dock)") {
    const RECT work{0, 0, 1000, 750};
    const RECT anchor{480, 760, 520, 800};
    const POINT origin = PlaceMenuPanel(SIZE{240, 300}, Anchored(MenuPlacement::AboveAnchor, anchor), work, kGap, kMargin);
    CHECK_EQ(origin.x, 380L);
    CHECK_EQ(origin.y, 454L);  // 760 - 6 - 300: stays one gap above the anchor
}

AETHER_TEST("glass menu placement: an anchor at the work-area top keeps the gap instead of the edge margin") {
    const RECT work{0, 40, 1000, 800};  // a bar reserves the top 40 pixels
    const RECT anchor{100, 8, 132, 40};
    const POINT origin = PlaceMenuPanel(SIZE{200, 200}, Anchored(MenuPlacement::BelowAnchor, anchor), work, kGap, kMargin);
    CHECK_EQ(origin.y, 46L);
}

AETHER_TEST("glass menu placement: flips to the other side when the preferred side has no room") {
    // Below would end at 700 + 6 + 300 > 792: flips above.
    const RECT low{500, 660, 540, 700};
    POINT origin = PlaceMenuPanel(SIZE{200, 300}, Anchored(MenuPlacement::BelowAnchor, low), kWork, kGap, kMargin);
    CHECK_EQ(origin.y, 660L - 6 - 300);
    // Above would start before 8: flips below.
    const RECT high{500, 50, 540, 90};
    origin = PlaceMenuPanel(SIZE{200, 300}, Anchored(MenuPlacement::AboveAnchor, high), kWork, kGap, kMargin);
    CHECK_EQ(origin.y, 96L);
    // Neither side fits: the larger side is used and the panel is clamped into the work area.
    const RECT middle{500, 380, 540, 420};
    origin = PlaceMenuPanel(SIZE{200, 500}, Anchored(MenuPlacement::BelowAnchor, middle), kWork, kGap, kMargin);
    CHECK(origin.y >= kWork.top + kMargin);
    CHECK(origin.y + 500 <= kWork.bottom - kMargin);
}

AETHER_TEST("glass menu placement: clamps horizontally to the work area with the edge margin") {
    const RECT right{960, 100, 1000, 140};
    POINT origin = PlaceMenuPanel(SIZE{300, 200}, Anchored(MenuPlacement::BelowAnchor, right), kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 1000L - 8 - 300);
    const RECT left{0, 100, 20, 140};
    origin = PlaceMenuPanel(SIZE{300, 200}, Anchored(MenuPlacement::BelowAnchor, left), kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 8L);
    // Wider than the work area: starts at the left margin.
    origin = PlaceMenuPanel(SIZE{2000, 200}, Anchored(MenuPlacement::BelowAnchor, right), kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 8L);
    // A work area on a monitor left of the primary one (negative coordinates).
    const RECT west{-1920, 0, 0, 1040};
    const RECT anchor{-60, 200, -20, 240};
    origin = PlaceMenuPanel(SIZE{300, 200}, Anchored(MenuPlacement::BelowAnchor, anchor), west, kGap, kMargin);
    CHECK_EQ(origin.x, -8L - 300);
}

AETHER_TEST("glass menu placement: at a point, flipping left and up at the edges") {
    MenuRequest request;
    request.placement = MenuPlacement::AtPoint;
    request.point = POINT{100, 100};
    POINT origin = PlaceMenuPanel(SIZE{200, 300}, request, kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 100L);
    CHECK_EQ(origin.y, 100L);
    request.point = POINT{900, 700};
    origin = PlaceMenuPanel(SIZE{200, 300}, request, kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 700L);
    CHECK_EQ(origin.y, 400L);
    // An empty anchor uses the point as well.
    MenuRequest empty = Anchored(MenuPlacement::BelowAnchor, RECT{});
    empty.point = POINT{50, 60};
    origin = PlaceMenuPanel(SIZE{200, 300}, empty, kWork, kGap, kMargin);
    CHECK_EQ(origin.x, 50L);
    CHECK_EQ(origin.y, 60L);
}

AETHER_TEST("glass menu width: minimum, request minimum, content, maximum and available width") {
    graphics::MenuTokens menu;
    CHECK_EQ(MenuPanelWidthDip(120.0f, 0, 96, menu, 0.0f), menu.minWidth);
    CHECK_EQ(MenuPanelWidthDip(260.0f, 0, 96, menu, 0.0f), 260.0f);
    CHECK_EQ(MenuPanelWidthDip(260.4f, 0, 96, menu, 0.0f), 261.0f);
    // A dropdown's field width in pixels, converted at the DPI.
    CHECK_EQ(MenuPanelWidthDip(120.0f, 480, 192, menu, 0.0f), 240.0f);
    CHECK_EQ(MenuPanelWidthDip(120.0f, 300, 144, menu, 0.0f), 200.0f);
    CHECK_EQ(MenuPanelWidthDip(300.0f, 300, 96, menu, 0.0f), 300.0f);
    // Clamped to the maximum (long labels wrap instead) and to the available width.
    CHECK_EQ(MenuPanelWidthDip(900.0f, 0, 96, menu, 0.0f), menu.maxWidth);
    CHECK_EQ(MenuPanelWidthDip(120.0f, 2000, 96, menu, 0.0f), menu.maxWidth);
    CHECK_EQ(MenuPanelWidthDip(300.0f, 0, 96, menu, 180.0f), 180.0f);
    // Invalid input falls back to the tokens.
    CHECK_EQ(MenuPanelWidthDip(std::numeric_limits<float>::quiet_NaN(), 0, 0, menu, 0.0f), menu.minWidth);
}

AETHER_TEST("glass submenu placement: right of the row, else left, clamped vertically") {
    const RECT row{100, 200, 340, 238};
    POINT origin = PlaceSubmenuPanel(SIZE{220, 150}, row, kWork, 4, kMargin);
    CHECK_EQ(origin.x, 336L);
    CHECK_EQ(origin.y, 200L);
    // No room on the right: opens to the left, overlapping the same amount.
    const RECT edge{600, 200, 840, 238};
    origin = PlaceSubmenuPanel(SIZE{220, 150}, edge, kWork, 4, kMargin);
    CHECK_EQ(origin.x, 604L - 220);
    // Near the bottom: moved up to stay inside the work area.
    const RECT low{100, 760, 340, 792};
    origin = PlaceSubmenuPanel(SIZE{220, 150}, low, kWork, 4, kMargin);
    CHECK_EQ(origin.y, 800L - 8 - 150);
    // Room on neither side: the larger side, clamped.
    const RECT wide{300, 100, 700, 138};
    origin = PlaceSubmenuPanel(SIZE{400, 150}, wide, kWork, 4, kMargin);
    CHECK(origin.x >= 8);
    CHECK(origin.x + 400 <= 992);
}

AETHER_TEST("glass menu keyboard: next row skips disabled entries and separators and wraps") {
    const auto entries = KeyboardMenu();
    CHECK_EQ(NextMenuRow(entries, kNoRow, 1), std::size_t{0});
    CHECK_EQ(NextMenuRow(entries, kNoRow, -1), std::size_t{4});
    CHECK_EQ(NextMenuRow(entries, 0, 1), std::size_t{3});  // skips the disabled row and the separator
    CHECK_EQ(NextMenuRow(entries, 3, 1), std::size_t{4});
    CHECK_EQ(NextMenuRow(entries, 4, 1), std::size_t{0});  // wraps past the disabled last row
    CHECK_EQ(NextMenuRow(entries, 0, -1), std::size_t{4});
    CHECK_EQ(NextMenuRow(entries, 3, -1), std::size_t{0});
    CHECK_EQ(NextMenuRow(entries, 99, 1), std::size_t{0});

    std::vector<MenuEntry> single{MenuEntry::Command(1, L"Only"), MenuEntry::Separator()};
    CHECK_EQ(NextMenuRow(single, 0, 1), std::size_t{0});  // wraps back to itself

    std::vector<MenuEntry> none{MenuEntry::Command(1, L"A", false), MenuEntry::Separator(), MenuEntry::Command(2, L"B", false)};
    CHECK_EQ(NextMenuRow(none, kNoRow, 1), kNoRow);
    CHECK_EQ(NextMenuRow(none, 0, -1), kNoRow);
    CHECK_EQ(NextMenuRow(std::span<const MenuEntry>{}, kNoRow, 1), kNoRow);
    CHECK_EQ(NextMenuRow(entries, 0, 0), kNoRow);
}

AETHER_TEST("glass menu keyboard: mnemonic rows are case-insensitive and enabled only") {
    const auto entries = KeyboardMenu();
    auto rows = MnemonicRows(entries, L'o');
    REQUIRE_EQ(rows.size(), std::size_t{1});
    CHECK_EQ(rows[0], std::size_t{0});
    rows = MnemonicRows(entries, L'O');
    REQUIRE_EQ(rows.size(), std::size_t{1});
    CHECK_EQ(rows[0], std::size_t{0});
    // "&Properties" is disabled, so only "&Pin to Dock" answers P.
    rows = MnemonicRows(entries, L'P');
    REQUIRE_EQ(rows.size(), std::size_t{1});
    CHECK_EQ(rows[0], std::size_t{3});
    rows = MnemonicRows(entries, L's');
    REQUIRE_EQ(rows.size(), std::size_t{1});
    CHECK_EQ(rows[0], std::size_t{4});  // submenus answer their mnemonic too
    CHECK(MnemonicRows(entries, L'q').empty());  // disabled
    CHECK(MnemonicRows(entries, L'x').empty());
    CHECK(MnemonicRows(entries, 0).empty());

    // Repeated mnemonics are all returned in order (the presenter cycles through them); "&&" is a
    // literal ampersand, not a mnemonic.
    std::vector<MenuEntry> repeated{MenuEntry::Command(1, L"&Small"), MenuEntry::Command(2, L"Salt && pepper"),
                                    MenuEntry::Command(3, L"&Smaller"), MenuEntry::Command(4, L"&Étendre")};
    rows = MnemonicRows(repeated, L'S');
    REQUIRE_EQ(rows.size(), std::size_t{2});
    CHECK_EQ(rows[0], std::size_t{0});
    CHECK_EQ(rows[1], std::size_t{2});
    rows = MnemonicRows(repeated, L'é');
    REQUIRE_EQ(rows.size(), std::size_t{1});
    CHECK_EQ(rows[0], std::size_t{3});
}
