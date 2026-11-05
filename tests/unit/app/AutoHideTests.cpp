// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// D47 Auto-Hide decisions for one bar, sample by sample (50 ms apart, as the host samples).
#include "AetherTest.h"
#include "app/AutoHide.h"
#include "app/PanelDismissal.h"
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

// D52, D60: the Top Bar's own timing. Samples are 50 ms apart, so index 5 is a quarter of a second after index 0.
AETHER_TEST("auto-hide: the Top Bar reveals only after a quarter of a second on the top edge") {
    Bar bar{AutoHideState{kTopBarAutoHideTiming}};
    REQUIRE(bar.Step(true));
    for (int i = 0; i < 5; ++i) CHECK(bar.Step(true, false, true));  // 0 to 200 ms there
    CHECK_FALSE(bar.Step(true, false, true));                        // 250 ms: revealed
    CHECK_FALSE(bar.Step(true, false, true));                        // and it stays while the pointer rests there
}

AETHER_TEST("auto-hide: leaving before the wait is up cancels the Top Bar's reveal") {
    Bar bar{AutoHideState{kTopBarAutoHideTiming}};
    REQUIRE(bar.Step(true));
    for (int i = 0; i < 5; ++i) REQUIRE(bar.Step(true, false, true));  // 200 ms: just short of it
    CHECK(bar.Step(true));                                             // moved away: still hidden
    // Coming back starts a whole new wait, not the remaining 50 ms.
    for (int i = 0; i < 5; ++i) CHECK(bar.Step(true, false, true));
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
    CHECK(top.Update(sample(2200, true)));          // a default 40 ms dwell would have revealed long ago
    CHECK_FALSE(top.Update(sample(2250, true)));
}

AETHER_TEST("auto-hide: the Dock keeps its short dwell") {
    CHECK_EQ(AutoHideTiming{}.revealDwellMs, 40u);
    CHECK_EQ(kTopBarAutoHideTiming.revealDwellMs, 250u);
    CHECK_EQ(kTopBarAutoHideTiming.hideDelayMs, AutoHideTiming{}.hideDelayMs);
}

// D60, the owner's rule: the very top of the screen, no margin, at any scale.
AETHER_TEST("auto-hide: only the top row of pixels is the Top Bar's edge") {
    const RECT monitor{-1920, 200, 0, 1280};  // negative and offset coordinates
    CHECK(PointerOnTopEdge(POINT{-960, 200}, monitor));
    CHECK(PointerOnTopEdge(POINT{-1920, 200}, monitor));
    CHECK(PointerOnTopEdge(POINT{-1, 200}, monitor));
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 201}, monitor));   // one pixel down is the application's
    CHECK_FALSE(PointerOnTopEdge(POINT{-960, 199}, monitor));   // on a monitor above: it left the edge
    CHECK_FALSE(PointerOnTopEdge(POINT{0, 200}, monitor));      // right is exclusive
    CHECK_FALSE(PointerOnTopEdge(POINT{-1921, 200}, monitor));
}

// The owner's complaint on 3.3: reaching for a maximized window's close button brought the bar over it.
AETHER_TEST("auto-hide: resting on an application's tabs or close button never brings the Top Bar") {
    const RECT monitor{0, 0, 1920, 1080};
    AutoHideState top{kTopBarAutoHideTiming};
    const auto at = [&](std::uint64_t now, LONG x, LONG y) {
        AutoHideSample value;
        value.enabled = true; value.covered = true; value.now = now;
        value.atEdge = PointerOnTopEdge(POINT{x, y}, monitor);
        return top.Update(value);
    };
    for (std::uint64_t now = 0; now < 5000; now += 50) CHECK(at(now, 1900, 1));  // one pixel below the top
    CHECK_EQ(top.EdgeRestMs(5000), 0u);
    CHECK(at(5000, 960, 0));         // the very top: the wait starts here
    CHECK(at(5200, 960, 0));
    CHECK(at(5220, 960, 1));         // down one pixel just before the end: cancelled
    CHECK_EQ(top.EdgeRestMs(5220), 0u);
    CHECK(at(5250, 960, 0));         // back: a whole new wait
    CHECK(at(5450, 960, 0));
    CHECK_FALSE(at(5500, 960, 0));
    CHECK_EQ(top.EdgeRestMs(5500), 250u);
}

// D59: back on the desktop, both bars return, with windows still behind it. What counts as the desktop:
AETHER_TEST("auto-hide: only the desktop's own windows count as being back on the desktop") {
    CHECK(IsDesktopWindowClass(L"Progman"));
    CHECK(IsDesktopWindowClass(L"WorkerW"));
    for (const auto* other : {L"", L"Chrome_WidgetWin_1", L"ApplicationFrameWindow", L"Shell_TrayWnd", L"CabinetWClass", L"progman"})
        CHECK_FALSE(IsDesktopWindowClass(other));
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

AETHER_TEST("panels: only a button going down is a press") {
    CHECK(IsRawMousePress(RI_MOUSE_LEFT_BUTTON_DOWN));
    CHECK(IsRawMousePress(RI_MOUSE_RIGHT_BUTTON_DOWN));
    CHECK(IsRawMousePress(RI_MOUSE_MIDDLE_BUTTON_DOWN));
    CHECK(IsRawMousePress(RI_MOUSE_BUTTON_4_DOWN));
    CHECK(IsRawMousePress(RI_MOUSE_BUTTON_5_DOWN));
    CHECK(IsRawMousePress(RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN));
    CHECK_FALSE(IsRawMousePress(0));                     // moving
    CHECK_FALSE(IsRawMousePress(RI_MOUSE_LEFT_BUTTON_UP));
    CHECK_FALSE(IsRawMousePress(RI_MOUSE_WHEEL));
}

AETHER_TEST("panels: the click whose press closed a panel does not open it again") {
    PressedAway guard;
    CHECK_FALSE(guard.Take(1000));          // nothing closed it: the toggle works
    guard.Mark(1000);
    CHECK(guard.Take(1200));                // the release of that press
    CHECK_FALSE(guard.Take(1250));          // only once: the next click is deliberate
    guard.Mark(2000);
    CHECK_FALSE(guard.Take(2500));          // too long after: a new click
    CHECK_FALSE(guard.Take(2501));
}
