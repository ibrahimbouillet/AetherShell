// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// D47 Auto-Hide decisions for one bar, sample by sample (50 ms apart, as the host samples).
#include "AetherTest.h"
#include "app/AutoHide.h"

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
