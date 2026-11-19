// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D84: Liquid's smart transparency. The mask is pure, so these tests give it own content and content behind and read
// the cells: full blur where own content lies over other content, light blur over the plain desktop, clear elsewhere,
// soft one-cell edges that never lower a cell.
#include "AetherTest.h"

#include "graphics/Theme.h"
#include "ui/form/SmartGlass.h"

#include <windows.h>

#include <algorithm>
#include <vector>

using namespace aether;
using namespace aether::ui::form;

namespace {

int Cell(const SmartMask& mask, int x, int y) { return mask.cells[static_cast<std::size_t>(y) * mask.columns + x]; }

LRESULT CALLBACK PlainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return ::DefWindowProcW(window, message, wParam, lParam);
}

HWND MakeWindow(const wchar_t* cls, HWND parent, DWORD style, int x, int y, int width, int height, const wchar_t* text = L"") {
    return ::CreateWindowExW(0, cls, text, style, x, y, width, height, parent, nullptr, ::GetModuleHandleW(nullptr), nullptr);
}

}  // namespace

AETHER_TEST("smart glass: the mask covers the client in whole cells") {
    const auto mask = ComputeSmartMask(100, 41, 8, {}, {}, 0);
    CHECK_EQ(mask.columns, 13);
    CHECK_EQ(mask.rows, 6);
    CHECK_EQ(mask.cellPx, 8);
    CHECK_EQ(mask.cells.size(), std::size_t{78});
    CHECK(std::all_of(mask.cells.begin(), mask.cells.end(), [](auto v) { return v == 0; }));   // nothing of its own: clear
}

AETHER_TEST("smart glass: an empty or degenerate client gives an empty mask") {
    CHECK(ComputeSmartMask(0, 100, 8, {}, {}, 0).cells.empty());
    CHECK(ComputeSmartMask(100, -1, 8, {}, {}, 0).cells.empty());
    CHECK(ComputeSmartMask(100, 100, 0, {}, {}, 0).cells.empty());
}

AETHER_TEST("smart glass: own content over other content blurs fully, over the plain desktop lightly") {
    // Own text at cells (2..3, 2); another window behind the left half only.
    const std::vector<RECT> own{{16, 16, 32, 24}, {64, 16, 80, 24}};
    const std::vector<RECT> busy{{0, 0, 48, 80}};
    const auto mask = ComputeSmartMask(96, 80, 8, own, busy, 0);
    CHECK_EQ(Cell(mask, 2, 2), 255);   // text over a window
    CHECK_EQ(Cell(mask, 3, 2), 255);
    CHECK_EQ(Cell(mask, 8, 2), 115);   // text over the desktop picture: 0.45
    CHECK_EQ(Cell(mask, 9, 2), 115);
    CHECK_EQ(Cell(mask, 0, 9), 0);     // nothing of its own, even over the window behind: clear
    CHECK_EQ(Cell(mask, 5, 7), 0);
}

AETHER_TEST("smart glass: edges fade as a gradient over three cells and never lower a cell") {
    const std::vector<RECT> own{{80, 80, 88, 88}};   // exactly cell (10, 10)
    const std::vector<RECT> busy{{0, 0, 200, 200}};
    const auto mask = ComputeSmartMask(168, 168, 8, own, busy, 0);
    CHECK_EQ(Cell(mask, 10, 10), 255);
    // Falling with the distance, cell by cell, to clear four cells away.
    CHECK(Cell(mask, 9, 10) > Cell(mask, 8, 10));
    CHECK(Cell(mask, 8, 10) > Cell(mask, 7, 10));
    CHECK(Cell(mask, 7, 10) > 0);
    CHECK_EQ(Cell(mask, 6, 10), 0);
    CHECK_EQ(Cell(mask, 10, 14), 0);
    CHECK(Cell(mask, 9, 10) < 255);
    // The same in every direction.
    CHECK_EQ(Cell(mask, 11, 10), Cell(mask, 9, 10));
    CHECK_EQ(Cell(mask, 10, 12), Cell(mask, 10, 8));
}

AETHER_TEST("smart glass: own content is widened by the margin and clipped to the client") {
    const std::vector<RECT> own{{40, 40, 48, 48}, {-50, -50, 4, 4}, {500, 500, 600, 600}};
    const auto mask = ComputeSmartMask(96, 96, 8, own, {}, 9);
    // 40-9 = 31 reaches cell 3; 48+9 = 57 reaches cell 7.
    CHECK_EQ(Cell(mask, 3, 5), 115);
    CHECK_EQ(Cell(mask, 7, 5), 115);
    CHECK_EQ(Cell(mask, 0, 0), 115);   // the part inside the client of a rectangle that starts outside it
    CHECK_EQ(Cell(mask, 11, 11), 0);   // entirely outside the client: nothing
}

AETHER_TEST("smart glass: custom levels and out-of-range levels") {
    const std::vector<RECT> own{{0, 0, 96, 96}};
    const std::vector<RECT> busy{{0, 0, 48, 96}};
    const auto mask = ComputeSmartMask(96, 96, 8, own, busy, 0, SmartGlassLevels{0.5f, 0.0f});
    CHECK_EQ(Cell(mask, 0, 0), 128);
    CHECK_EQ(Cell(mask, 11, 0), 0);
    const auto clamped = ComputeSmartMask(96, 96, 8, own, busy, 0, SmartGlassLevels{7.0f, -3.0f});
    CHECK_EQ(Cell(clamped, 0, 0), 255);
    CHECK_EQ(Cell(clamped, 11, 0), 0);
}

AETHER_TEST("smart glass: equal inputs give equal masks, so an unchanged scene is not redrawn") {
    const std::vector<RECT> own{{10, 10, 60, 30}};
    const std::vector<RECT> busy{{0, 0, 30, 30}};
    CHECK(ComputeSmartMask(120, 80, 8, own, busy, 6) == ComputeSmartMask(120, 80, 8, own, busy, 6));
    CHECK_FALSE(ComputeSmartMask(120, 80, 8, own, busy, 6) == ComputeSmartMask(120, 80, 8, own, {}, 6));
}

AETHER_TEST("smart glass: own content is the visible leaf controls, clipped by what holds them") {
    WNDCLASSW cls{};
    cls.lpfnWndProc = &PlainProc;
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.lpszClassName = L"AetherSmartGlassTestForm";
    ::RegisterClassW(&cls);
    const HWND form = MakeWindow(cls.lpszClassName, nullptr, WS_POPUP, 0, 0, 400, 300);
    REQUIRE(form != nullptr);
    // A label, an empty label (nothing), a hidden button (nothing) and a viewport holding a long canvas.
    MakeWindow(L"STATIC", form, WS_CHILD | WS_VISIBLE, 10, 10, 100, 20, L"Title");
    MakeWindow(L"STATIC", form, WS_CHILD | WS_VISIBLE, 10, 40, 100, 20);
    MakeWindow(L"BUTTON", form, WS_CHILD, 10, 70, 100, 20, L"Hidden");
    const HWND viewport = MakeWindow(cls.lpszClassName, form, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 150, 100, 200, 100);
    const HWND canvas = MakeWindow(cls.lpszClassName, viewport, WS_CHILD | WS_VISIBLE, 0, -40, 200, 400);
    MakeWindow(L"BUTTON", canvas, WS_CHILD | WS_VISIBLE, 20, 20, 80, 30, L"Half shown");   // canvas 20..50 -> viewport -20..10
    MakeWindow(L"BUTTON", canvas, WS_CHILD | WS_VISIBLE, 20, 300, 80, 30, L"Scrolled away");
    auto own = OwnContentRects(form);
    std::sort(own.begin(), own.end(), [](const RECT& a, const RECT& b) { return a.top < b.top; });
    REQUIRE_EQ(own.size(), std::size_t{2});
    CHECK_EQ(own[0].left, 10);
    CHECK_EQ(own[0].top, 10);
    CHECK_EQ(own[0].right, 110);
    CHECK_EQ(own[0].bottom, 30);
    CHECK_EQ(own[1].left, 170);   // clipped by the viewport: only its lower part shows
    CHECK_EQ(own[1].top, 100);
    CHECK_EQ(own[1].right, 250);
    CHECK_EQ(own[1].bottom, 110);
    ::DestroyWindow(form);
}

AETHER_TEST("smart glass: a hidden window has nothing of its own and nothing behind") {
    CHECK(OwnContentRects(nullptr).empty());
    CHECK(WindowsBehind(nullptr).empty());
}
