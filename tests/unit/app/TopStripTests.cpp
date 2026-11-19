// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// D85: where a window that reaches into the Top Bar's strip is put back (pure geometry, physical pixels).
#include "AetherTest.h"
#include "app/TopStrip.h"

#include <cstdint>

using namespace aether::app;

namespace {

// A 1920 x 1080 monitor, a 48-pixel strip at its top, 18-pixel side insets.
constexpr RECT kMonitor{0, 0, 1920, 1080};
constexpr RECT kStrip{0, 0, 1920, 48};
constexpr RECT kWork{18, 48, 1902, 1080};

StripWindowFacts Window(RECT frame, LONG border = 7, bool maximized = false, bool resizable = true, RECT work = kWork) {
    StripWindowFacts facts;
    facts.frame = frame;
    facts.outer = RECT{frame.left - border, frame.top, frame.right + border, frame.bottom + border};
    facts.monitor = kMonitor;
    facts.work = work;
    facts.maximized = maximized;
    facts.resizable = resizable;
    return facts;
}

bool Equal(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }

}  // namespace

AETHER_TEST("top strip: a window clear of the strip stays where it is") {
    CHECK_FALSE(FitBelowStrip(Window({200, 48, 900, 700}), kStrip).has_value());
    CHECK_FALSE(FitBelowStrip(Window({200, 300, 900, 700}), kStrip).has_value());
}

AETHER_TEST("top strip: a window dragged into the strip moves down by as much, keeping its size") {
    const auto facts = Window({200, 10, 900, 610});
    const auto fitted = FitBelowStrip(facts, kStrip);
    REQUIRE(fitted.has_value());
    CHECK_EQ(fitted->top - facts.outer.top, 38);
    CHECK_EQ(fitted->bottom - fitted->top, facts.outer.bottom - facts.outer.top);
    CHECK_EQ(fitted->left, facts.outer.left);
    CHECK_EQ(fitted->right, facts.outer.right);
}

AETHER_TEST("top strip: a window above the screen's top edge comes down below the strip") {
    const auto fitted = FitBelowStrip(Window({200, -30, 900, 400}), kStrip);
    REQUIRE(fitted.has_value());
    CHECK_EQ(fitted->top, 48);   // the frame's top is the window's top here (no invisible border above)
}

AETHER_TEST("top strip: a tall resizable window is made shorter to end at the work area's bottom") {
    const auto facts = Window({100, 0, 1000, 1080});
    const auto fitted = FitBelowStrip(facts, kStrip);
    REQUIRE(fitted.has_value());
    CHECK_EQ(fitted->top, 48);
    CHECK_EQ(fitted->bottom, 1080 + 7);   // the frame ends at the work area's bottom, its border below it as before
}

AETHER_TEST("top strip: a tall fixed-size window keeps its size") {
    const auto facts = Window({100, 0, 1000, 1080}, 7, false, false);
    const auto fitted = FitBelowStrip(facts, kStrip);
    REQUIRE(fitted.has_value());
    CHECK_EQ(fitted->bottom - fitted->top, facts.outer.bottom - facts.outer.top);
}

AETHER_TEST("top strip: a maximized window under the strip is fitted to the work area") {
    StripWindowFacts facts;
    facts.frame = RECT{18, 0, 1902, 1080};
    facts.outer = RECT{7, -11, 1913, 1091};
    facts.monitor = kMonitor;
    facts.work = kWork;
    facts.maximized = true;
    const auto fitted = FitBelowStrip(facts, kStrip);
    REQUIRE(fitted.has_value());
    CHECK(Equal(*fitted, RECT{7, 37, 1913, 1091}));
}

AETHER_TEST("top strip: a maximized window waits while the work area still includes the strip") {
    StripWindowFacts facts = Window({18, 0, 1902, 1080}, 11, true, true, RECT{18, 0, 1902, 1080});
    CHECK_FALSE(FitBelowStrip(facts, kStrip).has_value());
}

AETHER_TEST("top strip: a maximized window already in the work area is left alone") {
    StripWindowFacts facts;
    facts.frame = kWork;
    facts.outer = RECT{7, 37, 1913, 1091};
    facts.monitor = kMonitor;
    facts.work = kWork;
    facts.maximized = true;
    CHECK_FALSE(FitBelowStrip(facts, kStrip).has_value());
}

AETHER_TEST("top strip: full screen and other monitors are never touched") {
    CHECK_FALSE(FitBelowStrip(Window({0, 0, 1920, 1080}, 0), kStrip).has_value());
    StripWindowFacts other = Window({2100, 0, 2800, 600});
    other.monitor = RECT{1920, 0, 3840, 1080};
    other.work = RECT{1920, 0, 3840, 1040};
    CHECK_FALSE(FitBelowStrip(other, kStrip).has_value());
}

AETHER_TEST("top strip: no strip, no move") {
    CHECK_FALSE(FitBelowStrip(Window({200, 10, 900, 610}), RECT{}).has_value());
}

AETHER_TEST("top strip: a window that keeps going back is left alone after three moves a minute") {
    StripRetryBudget budget(3, 60000);
    const HWND a = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x10));
    const HWND b = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x20));
    CHECK(budget.Take(a, 1000));
    CHECK(budget.Take(a, 2000));
    CHECK(budget.Take(a, 3000));
    CHECK_FALSE(budget.Take(a, 4000));
    CHECK(budget.Take(b, 4000));          // another window has its own budget
    CHECK(budget.Take(a, 61500));         // the first move is a minute old
    CHECK_FALSE(budget.Take(a, 61600));
}
