// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/WindowCorners.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace aether;
using namespace aether::app;
namespace {
// A maximized application window between the bars of a 1920 x 1080 monitor, as Windows reports one: its
// bounds overflow the frame by the hidden resize border, and the work area leaves room for the bars.
CornerWindowFacts Maximized() {
    CornerWindowFacts facts;
    facts.visible = true; facts.maximized = true; facts.topLevel = true;
    facts.monitor = {0, 0, 1920, 1080};
    facts.work = {18, 63, 1902, 990};
    facts.frame = facts.work;
    facts.bounds = {facts.work.left - 8, facts.work.top - 8, facts.work.right + 8, facts.work.bottom + 8};
    facts.dpi = 144;
    return facts;
}
// One mark per pixel of a `width` x `height` outline from `rects`; a pixel named twice counts twice.
std::vector<int> Paint(const std::vector<RECT>& rects, int width, int height) {
    std::vector<int> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (const RECT& rect : rects) {
        REQUIRE(rect.left >= 0); REQUIRE(rect.top >= 0); REQUIRE(rect.right <= width); REQUIRE(rect.bottom <= height);
        REQUIRE(rect.right > rect.left); REQUIRE(rect.bottom > rect.top);
        for (LONG y = rect.top; y < rect.bottom; ++y) for (LONG x = rect.left; x < rect.right; ++x)
            ++pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
    return pixels;
}
// The definition the rectangles must reproduce: a pixel belongs to the rounded outline of radius `radius`
// (inset by `inset`) when its centre lies inside it.
bool InsideOutline(int x, int y, int width, int height, int radius, int inset) {
    if (x < inset || y < inset || x >= width - inset || y >= height - inset) return false;
    const double r = static_cast<double>(radius) - inset;
    const double px = x + 0.5, py = y + 0.5;
    const double cx = px < radius ? radius : px > width - radius ? width - radius : px;
    const double cy = py < radius ? radius : py > height - radius ? height - radius : py;
    if (cx == px || cy == py) return true;   // not in a corner
    if (!(r > 0.0)) return false;
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
}
}

AETHER_TEST("window corners: a visible maximized window between the bars is served, whatever kind of application it is (D72)") {
    const auto facts = Maximized();
    const auto plain = ClassifyCornerWindow(facts);
    CHECK(plain.eligible);
    CHECK(::EqualRect(&plain.frame, &facts.frame) != FALSE);
    CHECK_EQ(plain.dpi, 144u);
    // Nothing about the application is asked or needed: no process, privilege, frame style or rendering family.
    // Only what is not a maximized application window in view is left out.
    const auto without = [](void (*change)(CornerWindowFacts&)) { auto facts = Maximized(); change(facts); return ClassifyCornerWindow(facts).eligible; };
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.visible = false; }));
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.minimized = true; }));
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.maximized = false; }));   // a window merely sized to the work area: Windows rounds it
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.topLevel = false; }));
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.cloaked = true; }));      // on another virtual desktop
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.toolWindow = true; }));
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.clickThrough = true; }));
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.own = true; }));          // the guard's own pieces
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.dpi = 0; }));
    CHECK_FALSE(without([](CornerWindowFacts& f) { f.frame = {0, 0, 0, 0}; }));
}

AETHER_TEST("window corners: a maximize that fills the monitor takes the screen and stays square (D56)") {
    auto facts = Maximized();
    facts.work = facts.monitor;   // both bars auto-hidden: nothing is reserved
    facts.frame = facts.monitor;
    facts.bounds = {-8, -8, 1928, 1088};
    CHECK_FALSE(ClassifyCornerWindow(facts).eligible);
    // Only one bar hidden: the window still sits against the other, and is rounded.
    facts.work = {0, 0, 1920, 990};
    facts.frame = facts.work;
    facts.bounds = {-8, -8, 1928, 998};
    CHECK(ClassifyCornerWindow(facts).eligible);
}

AETHER_TEST("window corners: a window with a shape of its own keeps it; a plain rectangle around the frame is no shape") {
    auto facts = Maximized();
    // A browser keeps its maximized window to the work area with a plain rectangular region: still served.
    facts.regionKind = SIMPLEREGION;
    facts.regionBox = {8, 8, 8 + (facts.work.right - facts.work.left), 8 + (facts.work.bottom - facts.work.top)};
    CHECK(ClassifyCornerWindow(facts).eligible);
    // A rectangle that hides part of the frame is the application's own cut.
    facts.regionBox.right -= 40;
    CHECK_FALSE(ClassifyCornerWindow(facts).eligible);
    // A real shape (Aether's own maximized Settings rounds itself; a clip left by an earlier version): left alone.
    facts.regionKind = COMPLEXREGION;
    facts.regionBox = {8, 8, 8 + (facts.work.right - facts.work.left), 8 + (facts.work.bottom - facts.work.top)};
    CHECK_FALSE(ClassifyCornerWindow(facts).eligible);
    facts.regionKind = NULLREGION;
    CHECK_FALSE(ClassifyCornerWindow(facts).eligible);
}

AETHER_TEST("window corners: the outline is the visible frame inside the work area, with a 12 DIP radius and a one DIP rim") {
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        auto facts = Maximized();
        facts.dpi = dpi;
        const auto observed = ClassifyCornerWindow(facts);
        REQUIRE(observed.eligible);
        const auto rim = CornerFrameFor(observed);
        REQUIRE(rim.has_value());
        CHECK(::EqualRect(&rim->bounds, &facts.work) != FALSE);   // screen pixels
        CHECK_EQ(rim->radiusPx, static_cast<int>(std::lround(12.0 * dpi / 96.0)));
        CHECK_EQ(rim->widthPx, CornerRimWidthPx(dpi));
        CHECK(rim->widthPx >= 1);
    }
    CHECK_EQ(CornerRimWidthPx(0), 1); CHECK_EQ(CornerRimWidthPx(96), 1); CHECK_EQ(CornerRimWidthPx(120), 1); CHECK_EQ(CornerRimWidthPx(144), 2); CHECK_EQ(CornerRimWidthPx(192), 2);
    // A frame that spills over the work area is cut to it; one outside it has no outline at all.
    auto facts = Maximized();
    facts.frame = {0, 40, 1920, 1000};
    const auto cut = CornerFrameFor(ClassifyCornerWindow(facts));
    REQUIRE(cut.has_value());
    CHECK(::EqualRect(&cut->bounds, &facts.work) != FALSE);
    CornerWindowObservation nowhere;
    nowhere.bounds = {0, 0, 100, 100}; nowhere.frame = {0, 0, 100, 100}; nowhere.work = {500, 500, 900, 900};
    CHECK_FALSE(CornerFrameFor(nowhere).has_value());
}

AETHER_TEST("window corners: the corner pieces and the rim tile the outline's edge exactly, pixel for pixel") {
    struct Case { int width, height, radius, rim; };
    for (const auto& c : {Case{400, 300, 18, 1}, Case{401, 299, 18, 2}, Case{1884, 927, 12, 1}, Case{640, 480, 24, 2}, Case{97, 61, 36, 3},
                          Case{64, 40, 15, 1}, Case{50, 50, 25, 4}, Case{300, 200, 1, 1}, Case{300, 200, 2, 3}, Case{31, 9, 12, 1}}) {
        const int radius = std::min({c.radius, c.width / 2, c.height / 2});
        const auto pieces = Paint(CornerPieceRects(c.width, c.height, c.radius), c.width, c.height);
        const auto rim = Paint(CornerRimRects(c.width, c.height, c.radius, c.rim), c.width, c.height);
        int wrong = 0, rimPixels = 0, piecePixels = 0;
        for (int y = 0; y < c.height; ++y) for (int x = 0; x < c.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(c.width) + static_cast<std::size_t>(x);
            const bool outline = InsideOutline(x, y, c.width, c.height, radius, 0);
            const bool inside = InsideOutline(x, y, c.width, c.height, radius, c.rim);
            const int wantPiece = outline ? 0 : 1, wantRim = outline && !inside ? 1 : 0;
            if (pieces[index] != wantPiece || rim[index] != wantRim) ++wrong;   // also catches a pixel named twice
            piecePixels += pieces[index]; rimPixels += rim[index];
        }
        CHECK_EQ(wrong, 0);
        CHECK(rimPixels > 0);
        if (radius >= 3) CHECK(piecePixels > 0);   // a corner of a pixel or two has nothing outside its arc
        // The pieces are four corners only: nothing of them along the sides.
        for (const RECT& rect : CornerPieceRects(c.width, c.height, c.radius)) {
            CHECK((rect.right <= radius || rect.left >= c.width - radius));
            CHECK((rect.bottom <= radius || rect.top >= c.height - radius));
        }
        // The rim is continuous: every edge's middle belongs to it, and round a corner each row of it touches the next.
        const auto in = [&](int x, int y) { return rim[static_cast<std::size_t>(y) * static_cast<std::size_t>(c.width) + static_cast<std::size_t>(x)] == 1; };
        CHECK(in(c.width / 2, 0)); CHECK(in(c.width / 2, c.height - 1)); CHECK(in(0, c.height / 2)); CHECK(in(c.width - 1, c.height / 2));
        int broken = 0, previousFirst = 0, previousLast = -1;
        for (int y = 0; y < radius; ++y) {
            int first = -1, last = -1;
            for (int x = 0; x <= radius && x < c.width; ++x) if (in(x, y)) { if (first < 0) first = x; last = x; }
            if (first < 0) { ++broken; continue; }
            if (y > 0 && (first > previousLast + 1 || last < previousFirst - 1)) ++broken;   // not even corner to corner
            previousFirst = first; previousLast = last;
        }
        CHECK_EQ(broken, 0);
    }
    // No radius: no corner piece, and the rim is the plain rectangle's border.
    CHECK(CornerPieceRects(200, 100, 0).empty());
    const auto square = Paint(CornerRimRects(200, 100, 0, 2), 200, 100);
    CHECK_EQ(static_cast<int>(std::count(square.begin(), square.end(), 1)), 200 * 100 - 196 * 96);
    CHECK_EQ(static_cast<int>(std::count_if(square.begin(), square.end(), [](int value) { return value > 1; })), 0);
    // Nothing to draw gives nothing, never a rectangle outside the outline.
    CHECK(CornerPieceRects(0, 100, 12).empty()); CHECK(CornerPieceRects(100, -5, 12).empty());
    CHECK(CornerRimRects(100, 100, 12, 0).empty()); CHECK(CornerRimRects(0, 0, 12, 1).empty());
}

AETHER_TEST("window corners: internal dispatch rejects malformed handles and preserves normal arguments") {
    auto normal = DispatchWindowCornerGuard({L"--settings"}); REQUIRE(normal.ok()); CHECK_FALSE(normal->has_value());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard"}).ok());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard", L"0", L"1", L"2"}).ok());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard", L"1x", L"1", L"2"}).ok());
    CHECK_FALSE(DispatchWindowCornerGuard({L"--window-corner-guard", L"9999999999999999999999", L"1", L"2"}).ok());
}
AETHER_TEST("window corners: isolated controller refuses native access without spawning a child") {
    WindowCorners controller(false, L"not-an-executable");
    CHECK_FALSE(controller.Request(true).ok()); REQUIRE(controller.Request(false).ok());
    controller.Stop(); CHECK_FALSE(controller.Request(true).ok());
}
AETHER_TEST("window corners: helper start failures are bounded, reported and retried only with budget (AS4-04)") {
    WindowCornersOptions options; options.firstRetryDelay = std::chrono::milliseconds(5); options.maximumAttempts = 3;
    const auto missing = std::filesystem::temp_directory_path() / L"aether-missing-corner-helper" / L"AetherShellApp.exe";
    WindowCorners corners(true, missing, options);
    CHECK(corners.Health().state == CornerGuardState::Off);
    REQUIRE(corners.Request(true).ok());
    const auto wait = [&](auto predicate) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate() && std::chrono::steady_clock::now() < end) { corners.Poll(); ::Sleep(5); }
        return predicate();
    };
    REQUIRE(wait([&] { return corners.Health().failures >= 3; }));
    const auto health = corners.Health();
    CHECK(health.wanted); CHECK(health.state == CornerGuardState::Failed); CHECK_EQ(health.failures, 3u);
    // The budget is spent: further supervision and identical requests do not start it again.
    for (int i = 0; i < 20; ++i) { corners.Poll(); REQUIRE(corners.Request(true).ok()); ::Sleep(2); }
    CHECK_EQ(corners.Health().failures, 3u);
    // A fresh request (off, then on) gets a fresh budget.
    REQUIRE(corners.Request(false).ok());
    REQUIRE(wait([&] { return corners.Health().state == CornerGuardState::Off; }));
    REQUIRE(corners.Request(true).ok());
    REQUIRE(wait([&] { return corners.Health().failures >= 1 && corners.Health().state == CornerGuardState::Failed; }));
    corners.Stop();
    CHECK(corners.Health().state == CornerGuardState::Off);
    CHECK_FALSE(corners.Request(true).ok());
}
AETHER_TEST("window corners: isolated sessions never start a helper") {
    WindowCorners corners(false, std::filesystem::temp_directory_path() / L"AetherShellApp.exe");
    CHECK_EQ(corners.Request(true).error().code, ErrorCode::AccessDenied);
    CHECK(corners.Health().state == CornerGuardState::Off);
}
