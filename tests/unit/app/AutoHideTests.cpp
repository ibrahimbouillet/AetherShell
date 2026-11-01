// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// D47 Auto-Hide decisions for one bar, sample by sample (50 ms apart, as the host samples).
#include "AetherTest.h"
#include "app/AutoHide.h"
#include "app/TouchEdgeReveal.h"

#include <limits>

using namespace aether;
using namespace aether::app;

namespace {

struct Bar {
    AutoHideState state;
    std::uint64_t now = 1000;
    bool Step(bool covered, bool engaged = false, bool atEdge = false, bool enabled = true) {
        AutoHideSample sample;
        sample.enabled = enabled;
        sample.covered = covered;
        sample.engaged = engaged;
        sample.atEdge = atEdge;
        sample.now = now;
        now += 50;
        return state.Update(sample);
    }
    [[nodiscard]] bool Hidden() const noexcept { return state.Hidden(); }
};

}  // namespace

AETHER_TEST("auto-hide: off, or on the desktop, the bar stays; an application window over it hides it") {
    Bar bar;
    CHECK_FALSE(bar.Step(true, false, false, false));  // setting off: always shown
    CHECK_FALSE(bar.Step(false));                      // on, desktop uncovered: shown
    CHECK(bar.Step(true));                             // an application window covers it: hidden
    CHECK(bar.Hidden());
    CHECK_FALSE(bar.Step(false));                      // back to the desktop: shown again, setting kept
    CHECK(bar.Step(true));                             // covered again: hidden again
}

AETHER_TEST("auto-hide: resting on the screen edge reveals it; sweeping across the edge does not") {
    Bar bar;
    REQUIRE(bar.Step(true));
    CHECK(bar.Step(true, false, true));          // first touch of the edge: not yet
    CHECK_FALSE(bar.Step(true, false, true));    // still there after the dwell: revealed
    // Leaving the edge without using the bar: it stays a moment, then hides again.
    CHECK_FALSE(bar.Step(true));
    for (int i = 0; i < 14; ++i) (void)bar.Step(true);  // past the 700 ms hide delay
    CHECK(bar.Hidden());
    // A single sample on the edge (pointer passing through) never reveals it.
    CHECK(bar.Step(true, false, true));
    CHECK(bar.Step(true, false, false));
    CHECK(bar.Step(true, false, true));
}

AETHER_TEST("auto-hide: a bar in use stays until a moment after the use ends") {
    Bar bar;
    REQUIRE(bar.Step(true));
    (void)bar.Step(true, false, true);
    REQUIRE_FALSE(bar.Step(true, false, true));  // revealed at the edge
    // Pointer over it, a menu or a panel: it stays however long that lasts.
    for (int i = 0; i < 40; ++i) CHECK_FALSE(bar.Step(true, true));
    // Use ends: still shown for the hide delay (700 ms), then hidden.
    int shown = 0;
    while (!bar.Step(true) && shown < 100) ++shown;
    CHECK(shown >= 12);
    CHECK(shown <= 15);
    CHECK(bar.Hidden());
}

AETHER_TEST("auto-hide: a bar in use when a window comes over it is not pulled away") {
    Bar bar;
    CHECK_FALSE(bar.Step(false, true));  // on the desktop, the pointer is on the Dock
    CHECK_FALSE(bar.Step(true, true));   // an application opens over it while the pointer is still there
    CHECK_FALSE(bar.Step(true));         // the pointer left: a moment more
    for (int i = 0; i < 14; ++i) (void)bar.Step(true);
    CHECK(bar.Hidden());
    // Turning the setting off brings it back at once and forgets the reveal.
    CHECK_FALSE(bar.Step(true, false, false, false));
    CHECK(bar.Step(true));
}

// D52, D59: the Top Bar's own timing. Samples are 50 ms apart, so index 10 is half a second after index 0.
AETHER_TEST("auto-hide: the Top Bar reveals only after half a second in its place") {
    Bar bar{AutoHideState{kTopBarAutoHideTiming}};
    REQUIRE(bar.Step(true));
    for (int i = 0; i < 10; ++i) CHECK(bar.Step(true, false, true));  // 0 to 450 ms there
    CHECK_FALSE(bar.Step(true, false, true));                         // 500 ms: revealed
    CHECK_FALSE(bar.Step(true, false, true));                         // and it stays while the pointer rests there
}

AETHER_TEST("auto-hide: leaving before the wait is up cancels the Top Bar's reveal") {
    Bar bar{AutoHideState{kTopBarAutoHideTiming}};
    REQUIRE(bar.Step(true));
    for (int i = 0; i < 10; ++i) REQUIRE(bar.Step(true, false, true));  // 450 ms: just short of it
    CHECK(bar.Step(true));                                              // moved away: still hidden
    // Coming back starts a whole new wait, not the remaining 50 ms.
    for (int i = 0; i < 10; ++i) CHECK(bar.Step(true, false, true));
    CHECK_FALSE(bar.Step(true, false, true));
}

AETHER_TEST("auto-hide: the Top Bar in use does not flicker when the pointer moves up onto its edge") {
    Bar bar{AutoHideState{kTopBarAutoHideTiming}};
    REQUIRE(bar.Step(true));
    REQUIRE_FALSE(bar.Step(true, true));  // the pointer is on the bar
    // Up onto the edge above it, for longer than the hide delay and the dwell together: never hidden
    // when the 700 ms delay ends with the dwell still running.
    for (int i = 0; i < 70; ++i) CHECK_FALSE(bar.Step(true, false, true));
    // Away from both: the ordinary hide delay, then hidden.
    int shown = 0;
    while (!bar.Step(true) && shown < 100) ++shown;
    CHECK(shown >= 12);
    CHECK(shown <= 15);
    CHECK(bar.Hidden());
}

AETHER_TEST("auto-hide: the Top Bar's wait does not gate its other reasons to show") {
    Bar bar{AutoHideState{kTopBarAutoHideTiming}};
    REQUIRE(bar.Step(true));
    CHECK_FALSE(bar.Step(true, true));                 // a panel or keyboard use: at once
    for (int i = 0; i < 15; ++i) (void)bar.Step(true);
    REQUIRE(bar.Hidden());
    CHECK_FALSE(bar.Step(false));                      // the desktop shows: at once
    CHECK(bar.Step(true));                             // covered again: hidden at once
    CHECK_FALSE(bar.Step(true, false, false, false));  // setting off: at once
}

AETHER_TEST("auto-hide: resetting a bar forgets its state but keeps its own timing") {
    AutoHideState top{kTopBarAutoHideTiming};
    const auto sample = [](std::uint64_t now, bool atEdge) {
        AutoHideSample value;
        value.enabled = true; value.covered = true; value.atEdge = atEdge; value.now = now;
        return value;
    };
    (void)top.Update(sample(1000, true));
    top.Reset();
    CHECK(top.Update(sample(2000, true)));          // the wait restarted here
    CHECK(top.Update(sample(2450, true)));          // a default 40 ms dwell would have revealed long ago
    CHECK_FALSE(top.Update(sample(2500, true)));
}

AETHER_TEST("auto-hide: the Dock keeps its short dwell") {
    CHECK_EQ(AutoHideTiming{}.revealDwellMs, 40u);
    CHECK_EQ(kTopBarAutoHideTiming.revealDwellMs, 500u);
    CHECK_EQ(kTopBarAutoHideTiming.hideDelayMs, AutoHideTiming{}.hideDelayMs);
}

AETHER_TEST("auto-hide: the Top Bar's zone is where the bar appears, at any scale") {
    const RECT monitor{-1920, 200, 0, 1280};  // negative and offset coordinates
    const float reach = TopBarReachDip(6, 32);
    CHECK_EQ(reach, 38.0f);
    CHECK(PointerOnTopEdge(POINT{-960, 200}, monitor, 96, reach));
    CHECK(PointerOnTopEdge(POINT{-1920, 200}, monitor, 96, reach));
    CHECK(PointerOnTopEdge(POINT{-960, 237}, monitor, 96, reach));          // 38 DIPs at 100 %
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 238}, monitor, 96, reach));
    CHECK(PointerOnTopEdge(POINT{-960, 256}, monitor, 144, reach));         // 57 rows at 150 %
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 257}, monitor, 144, reach));
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 199}, monitor, 96, reach));    // on a monitor above: it left the edge
    CHECK_FALSE(PointerOnTopEdge(POINT{0, 200}, monitor, 96, reach));       // right is exclusive
    CHECK_FALSE(PointerOnTopEdge(POINT{-1921, 200}, monitor, 96, reach));
    CHECK(PointerOnTopEdge(POINT{-960, 271}, monitor, 144, reach + kTopEdgeWobbleDip));        // 72 rows at 150 %
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 272}, monitor, 144, reach + kTopEdgeWobbleDip));
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 199}, monitor, 144, reach + kTopEdgeWobbleDip));
    // The bar's own settings move the zone with it.
    CHECK(PointerOnTopEdge(POINT{-960, 257}, monitor, 96, TopBarReachDip(10, 48)));
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 258}, monitor, 96, TopBarReachDip(10, 48)));
}

// D58, the path measured on the owner's machine: at 150 % the hand stopped 52 pixels from the top, in the
// middle of where the bar appears, and rested there 2.6 s before going on to the very top. The bar came out
// 4.9 s after the hand arrived. It must come out half a second after, wherever in that place the hand stopped.
AETHER_TEST("auto-hide: a hand stopping where the bar appears gets it half a second later") {
    const RECT monitor{0, 0, 1920, 1080};
    const float reach = TopBarReachDip(6, 32);
    AutoHideState top{kTopBarAutoHideTiming};
    const auto at = [&](std::uint64_t now, LONG y) {
        const POINT pointer{960, y};
        AutoHideSample value;
        value.enabled = true; value.covered = true; value.now = now;
        value.atEdge = PointerOnTopEdge(pointer, monitor, 144, reach);
        value.nearEdge = PointerOnTopEdge(pointer, monitor, 144, reach + kTopEdgeWobbleDip);
        return top.Update(value);
    };
    CHECK(at(0, 127));           // on its way, rested just below: not yet
    CHECK(at(1950, 127));
    CHECK(at(2000, 52));         // there
    CHECK(at(2450, 52));
    CHECK_FALSE(at(2500, 52));   // and half a second later, out
    CHECK_EQ(top.EdgeRestMs(2500), 500u);
    // Passing through on the way to something below is no reason to come out.
    AutoHideState passing{kTopBarAutoHideTiming};
    const auto through = [&](std::uint64_t now, LONG y) {
        const POINT pointer{960, y};
        AutoHideSample value;
        value.enabled = true; value.covered = true; value.now = now;
        value.atEdge = PointerOnTopEdge(pointer, monitor, 144, reach);
        value.nearEdge = PointerOnTopEdge(pointer, monitor, 144, reach + kTopEdgeWobbleDip);
        return passing.Update(value);
    };
    for (std::uint64_t now = 0; now < 450; now += 50) CHECK(through(now, 30));
    CHECK(through(450, 200));
    CHECK(through(5000, 200));
}

// D59: back on the desktop, both bars return, with windows still behind it. What counts as the desktop:
AETHER_TEST("auto-hide: only the desktop's own windows count as being back on the desktop") {
    CHECK(IsDesktopWindowClass(L"Progman"));
    CHECK(IsDesktopWindowClass(L"WorkerW"));
    for (const auto* other : {L"", L"Chrome_WidgetWin_1", L"ApplicationFrameWindow", L"Shell_TrayWnd", L"CabinetWClass", L"progman"})
        CHECK_FALSE(IsDesktopWindowClass(other));
}

// D54: with the pointer resting by hand, one pixel of movement used to restart the wait. Here the hand
// wobbles every 300 ms throughout it.
AETHER_TEST("auto-hide: a wobbling hand still reveals the Top Bar on time, not later") {
    AutoHideState top{kTopBarAutoHideTiming};
    const auto sample = [](std::uint64_t now, bool atEdge, bool nearEdge) {
        AutoHideSample value;
        value.enabled = true; value.covered = true; value.atEdge = atEdge; value.nearEdge = nearEdge; value.now = now;
        return value;
    };
    std::uint64_t revealedAt = 0;
    for (std::uint64_t now = 1000; now < 9000 && !revealedAt; now += 50) {
        const bool wobbled = (now / 100) % 6 == 5;  // briefly a few pixels down, still by the edge
        if (!top.Update(sample(now, !wobbled, true))) revealedAt = now;
    }
    CHECK_EQ(revealedAt, 1500u);
    CHECK_EQ(top.EdgeRestMs(1500), 500u);
}

AETHER_TEST("auto-hide: moving really away still cancels, and being near never starts the wait") {
    AutoHideState top{kTopBarAutoHideTiming};
    const auto sample = [](std::uint64_t now, bool atEdge, bool nearEdge) {
        AutoHideSample value;
        value.enabled = true; value.covered = true; value.atEdge = atEdge; value.nearEdge = nearEdge; value.now = now;
        return value;
    };
    for (std::uint64_t now = 0; now < 4000; now += 50) CHECK(top.Update(sample(now, false, true)));  // a tab just below the edge
    CHECK_EQ(top.EdgeRestMs(4000), 0u);
    CHECK(top.Update(sample(4000, true, true)));
    CHECK(top.Update(sample(4400, false, true)));   // the wait is still running
    CHECK(top.Update(sample(4450, false, false)));  // gone, just before the end
    CHECK_EQ(top.EdgeRestMs(4450), 0u);
    CHECK(top.Update(sample(4500, false, true)));   // near again does not resume it
    CHECK(top.Update(sample(4550, true, true)));    // a whole new wait
    CHECK(top.Update(sample(5000, true, true)));
    CHECK_FALSE(top.Update(sample(5050, true, true)));
    // The Dock passes no near state: its behaviour is unchanged.
    AutoHideState dock;
    CHECK(dock.Update(sample(0, true, false)));
    CHECK(dock.Update(sample(20, false, false)));
    CHECK(dock.Update(sample(40, true, false)));
    CHECK_FALSE(dock.Update(sample(80, true, false)));
}

AETHER_TEST("touch edges: a swipe in from either edge reveals once; ordinary touches never do (FR-040)") {
    const double height = 720.0;
    EdgeSwipeRecognizer edges;
    // In from the top.
    CHECK_FALSE(edges.Contact(1, true, 500, 4, height, 1000).has_value());
    CHECK_FALSE(edges.Contact(1, true, 502, 30, height, 1050).has_value());
    const auto top = edges.Contact(1, true, 504, 60, height, 1100);
    REQUIRE(top.has_value());
    CHECK(*top == ScreenEdge::Top);
    CHECK_FALSE(edges.Contact(1, true, 504, 200, height, 1200).has_value());  // one contact, one reveal
    CHECK_FALSE(edges.Contact(1, false, 504, 200, height, 1300).has_value());
    // In from the bottom.
    CHECK_FALSE(edges.Contact(2, true, 500, 716, height, 2000).has_value());
    const auto bottom = edges.Contact(2, true, 500, 660, height, 2100);
    REQUIRE(bottom.has_value());
    CHECK(*bottom == ScreenEdge::Bottom);
    (void)edges.Contact(2, false, 500, 660, height, 2200);
    // A touch in the middle of the screen, however far it moves, is the application's.
    CHECK_FALSE(edges.Contact(3, true, 500, 300, height, 3000).has_value());
    CHECK_FALSE(edges.Contact(3, true, 500, 500, height, 3100).has_value());
    (void)edges.Contact(3, false, 500, 500, height, 3200);
    // A drag along the edge, a touch held too long and one moving outward are not swipes.
    CHECK_FALSE(edges.Contact(4, true, 100, 6, height, 4000).has_value());
    CHECK_FALSE(edges.Contact(4, true, 400, 60, height, 4100).has_value());
    (void)edges.Contact(4, false, 400, 60, height, 4200);
    CHECK_FALSE(edges.Contact(5, true, 500, 6, height, 5000).has_value());
    CHECK_FALSE(edges.Contact(5, true, 500, 80, height, 6000).has_value());
    (void)edges.Contact(5, false, 500, 80, height, 6100);
    // Invalid reports change nothing; lost lifts cannot accumulate without bound.
    CHECK_FALSE(edges.Contact(6, true, std::numeric_limits<double>::quiet_NaN(), 6, height, 7000).has_value());
    for (std::uint32_t id = 10; id < 200; ++id) CHECK_FALSE(edges.Contact(id, true, 100, 2, height, 8000).has_value());
}
