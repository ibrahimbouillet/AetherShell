// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "graphics/ScrollPhysics.h"

#include <cmath>
#include <limits>

using namespace aether::graphics;

namespace {
ScrollPhysics Model(double maximum = 2000.0, double viewport = 600.0) {
    ScrollPhysics physics;
    physics.SetExtent(maximum, viewport);
    return physics;
}
// Drags from `from` to `to` (finger coordinates) at a constant speed sampled at 120 Hz, ending at `end`.
double Swipe(ScrollPhysics& physics, double from, double to, double seconds, double start = 1.0) {
    physics.Grab(from, start);
    const int steps = std::max(1, static_cast<int>(seconds * 120.0));
    for (int i = 1; i <= steps; ++i) physics.Drag(from + (to - from) * i / steps, start + seconds * i / steps);
    return start + seconds;
}
// Runs frames at 60 Hz until rest; returns the simulated duration (or a large value if it never rests).
double RunToRest(ScrollPhysics& physics, double& now, double limit = 10.0) {
    const double begin = now;
    while (physics.Step(now += 1.0 / 60.0)) {
        CHECK(physics.Logical() >= 0.0); CHECK(physics.Logical() <= physics.Maximum());
        if (now - begin > limit) break;
    }
    return now - begin;
}
}  // namespace

AETHER_TEST("scroll physics: the content follows the finger exactly, independent of packet size") {
    auto coarse = Model(), fine = Model();
    coarse.JumpTo(300.0); fine.JumpTo(300.0);
    coarse.Grab(500.0, 1.0); coarse.Drag(380.0, 1.1);
    fine.Grab(500.0, 1.0);
    for (int i = 1; i <= 120; ++i) fine.Drag(500.0 - i, 1.0 + i * 0.001);
    CHECK_EQ(coarse.Visual(), 420.0);  // 300 + 120: the finger moved up 120 units
    CHECK_EQ(fine.Visual(), 420.0);
    fine.Drag(560.0, 1.2);             // reversal past the start is just as direct
    CHECK_EQ(fine.Visual(), 240.0);
    CHECK(fine.CurrentPhase() == ScrollPhysics::Phase::Tracking);
}

AETHER_TEST("scroll physics: release velocity flings, decelerates monotonically and stops without frames") {
    auto physics = Model(20000.0);
    physics.JumpTo(1000.0);
    double now = Swipe(physics, 800.0, 500.0, 0.15);  // 2000 units/s upwards
    physics.Release(now);
    REQUIRE(physics.CurrentPhase() == ScrollPhysics::Phase::Inertia);
    CHECK(std::abs(physics.Velocity() - 2000.0) < 60.0);
    double previousSpeed = std::abs(physics.Velocity()), previous = physics.Visual();
    while (physics.Step(now += 1.0 / 60.0)) {
        CHECK(std::abs(physics.Velocity()) <= previousSpeed + 1e-9);
        CHECK(physics.Visual() >= previous);
        previousSpeed = std::abs(physics.Velocity()); previous = physics.Visual();
    }
    CHECK(physics.CurrentPhase() == ScrollPhysics::Phase::Idle);
    // Exponential decay travels about v/k beyond the release point.
    CHECK(std::abs(physics.Visual() - (1300.0 + 2000.0 / physics.Parameters().deceleration)) < 40.0);
    CHECK_FALSE(physics.Step(now + 1.0));  // at rest no further frame is requested
}

AETHER_TEST("scroll physics: a rested contact or a slow release does not fling") {
    auto physics = Model();
    physics.JumpTo(500.0);
    double now = Swipe(physics, 800.0, 500.0, 0.15);
    physics.Release(now + 0.2);  // held still for 200 ms before lifting
    CHECK(physics.CurrentPhase() == ScrollPhysics::Phase::Idle);
    physics.Grab(500.0, 5.0); physics.Drag(499.0, 5.1);  // 10 units/s
    physics.Release(5.1);
    CHECK(physics.CurrentPhase() == ScrollPhysics::Phase::Idle);
}

AETHER_TEST("scroll physics: a regrab during a fling stops it where it is and can reverse it without a jump") {
    auto physics = Model(20000.0);
    physics.JumpTo(1000.0);
    double now = Swipe(physics, 900.0, 600.0, 0.1);
    physics.Release(now);
    for (int i = 0; i < 6; ++i) physics.Step(now += 1.0 / 60.0);
    REQUIRE(physics.Moving());
    const double before = physics.Visual();
    physics.Grab(300.0, now);
    CHECK_EQ(physics.Visual(), before);
    CHECK_EQ(physics.Velocity(), 0.0);
    physics.Drag(300.0, now + 0.01);
    CHECK_EQ(physics.Visual(), before);
    physics.Drag(350.0, now + 0.05);  // reverse: the finger moves down, the offset goes back
    CHECK_EQ(physics.Visual(), before - 50.0);
}

AETHER_TEST("scroll physics: overscroll resists, stays bounded and springs back to the exact bound") {
    auto physics = Model(1000.0, 600.0);
    physics.JumpTo(0.0);
    physics.Grab(200.0, 1.0);
    physics.Drag(500.0, 1.2);  // pulls 300 units past the top
    CHECK(physics.Visual() < 0.0);
    CHECK(-physics.Visual() < 300.0);  // resisted
    CHECK(-physics.Visual() <= physics.Parameters().maximumOverscroll);
    CHECK_EQ(physics.Logical(), 0.0);  // the logical (UIA) offset never leaves the range
    physics.Drag(5000.0, 1.3);
    CHECK(-physics.Visual() <= physics.Parameters().maximumOverscroll + 1e-9);
    // The displacement is continuous on the way back too: the inverse maps it to the same finger delta.
    const double held = physics.Visual();
    physics.Grab(0.0, 1.4); CHECK(std::abs(physics.Visual() - held) < 1e-9);
    physics.Release(1.4);
    REQUIRE(physics.CurrentPhase() == ScrollPhysics::Phase::Spring);
    double now = 1.4;
    const double duration = RunToRest(physics, now);
    CHECK(duration < 1.5);
    CHECK_EQ(physics.Visual(), 0.0);
    CHECK(physics.CurrentPhase() == ScrollPhysics::Phase::Idle);
}

AETHER_TEST("scroll physics: a fling into the end is caught by the spring within the overscroll bound") {
    auto physics = Model(1000.0, 600.0);
    physics.JumpTo(900.0);
    double now = Swipe(physics, 900.0, 400.0, 0.08);  // fast towards the end
    physics.Release(now);
    double furthest = 0.0;
    while (physics.Step(now += 1.0 / 60.0)) furthest = std::max(furthest, physics.Visual() - 1000.0);
    CHECK(furthest > 0.0);  // it did run past the end visually ...
    CHECK(furthest <= physics.Parameters().maximumOverscroll + 1e-9);
    CHECK_EQ(physics.Visual(), 1000.0);  // ... and rests exactly on it
}

AETHER_TEST("scroll physics: reduced motion keeps direct tracking but no fling, overscroll or bounce") {
    auto physics = Model(1000.0);
    physics.SetReducedMotion(true);
    physics.JumpTo(500.0);
    double now = Swipe(physics, 800.0, 600.0, 0.05);
    CHECK_EQ(physics.Visual(), 700.0);
    physics.Release(now);
    CHECK(physics.CurrentPhase() == ScrollPhysics::Phase::Idle);
    CHECK_EQ(physics.Visual(), 700.0);
    physics.Grab(0.0, 2.0); physics.Drag(-900.0, 2.1);
    CHECK_EQ(physics.Visual(), 1000.0);  // clamped, never displaced past the end
}

AETHER_TEST("scroll physics: a long frame gap settles instead of integrating; non-finite input is ignored") {
    auto physics = Model(20000.0);
    physics.JumpTo(1000.0);
    double now = Swipe(physics, 900.0, 500.0, 0.1);
    physics.Release(now);
    REQUIRE(physics.Moving());
    const double before = physics.Visual();
    CHECK_FALSE(physics.Step(now + 30.0));  // resumed from sleep
    CHECK(physics.CurrentPhase() == ScrollPhysics::Phase::Idle);
    CHECK_EQ(physics.Visual(), before);
    physics.Drag(std::numeric_limits<double>::quiet_NaN(), now + 31.0);
    physics.JumpTo(std::numeric_limits<double>::infinity());
    CHECK(std::isfinite(physics.Visual()));
    physics.SetExtent(100.0, 400.0);  // content shrank while resting
    CHECK(physics.Visual() <= 100.0);
}

AETHER_TEST("scroll physics: a release timestamp ahead of the frame clock does not stall the return") {
    auto physics = Model(1000.0, 600.0);
    physics.JumpTo(0.0);
    physics.Grab(0.0, 100.0);
    physics.Drag(200.0, 100.1);
    physics.Release(100.5);  // the contact's clock is ahead of the frames below
    REQUIRE(physics.CurrentPhase() == ScrollPhysics::Phase::Spring);
    double now = 90.0;
    CHECK(RunToRest(physics, now) < 1.5);
    CHECK_EQ(physics.Visual(), 0.0);
}

AETHER_TEST("touch gesture: a still press is a tap; a pan, an across move, a second contact or a cancel never is") {
    TouchGesture gesture({8.0, 0.5}, TouchAxis::Vertical);
    using Event = TouchGesture::Event;
    CHECK(gesture.Down(1, 100, 100, 0.0) == Event::None);
    CHECK(gesture.Move(1, 103, 104, 0.05) == Event::None);  // within slop
    CHECK(gesture.Up(1, 103, 104, 0.1) == Event::Tap);
    // Vertical pan
    gesture.Down(2, 100, 100, 1.0);
    CHECK(gesture.Move(2, 101, 120, 1.05) == Event::PanStarted);
    CHECK(gesture.Up(2, 101, 160, 1.2) == Event::PanEnded);
    // Horizontal movement on a vertical scroller cancels: no tap, no pan
    gesture.Down(3, 100, 100, 2.0);
    CHECK(gesture.Move(3, 130, 101, 2.05) == Event::Canceled);
    CHECK(gesture.Up(3, 100, 100, 2.1) == Event::None);
    // Second contact
    gesture.Down(4, 100, 100, 3.0);
    CHECK(gesture.Down(5, 300, 300, 3.01) == Event::Canceled);
    CHECK(gesture.Up(5, 300, 300, 3.02) == Event::None);
    CHECK(gesture.Up(4, 100, 100, 3.03) == Event::None);
    // Capture loss / hide
    gesture.Down(6, 100, 100, 4.0);
    CHECK(gesture.Cancel() == Event::Canceled);
    CHECK(gesture.Up(6, 100, 100, 4.1) == Event::None);
    CHECK(gesture.CurrentPhase() == TouchGesture::Phase::Idle);
}

AETHER_TEST("touch gesture: a hold is reported once and never also a tap; other pointers are ignored") {
    TouchGesture gesture({8.0, 0.5}, TouchAxis::Both);
    using Event = TouchGesture::Event;
    gesture.Down(7, 50, 50, 0.0);
    CHECK(gesture.Tick(0.3) == Event::None);
    CHECK(gesture.Up(8, 50, 50, 0.31) == Event::None);  // not the tracked contact
    CHECK(gesture.Tick(0.6) == Event::HoldStarted);
    CHECK(gesture.Tick(0.9) == Event::None);
    CHECK(gesture.Up(7, 50, 50, 1.0) == Event::HoldEnded);
    // A hold that then moves is canceled (no command), and any-direction pans use the distance.
    gesture.Down(9, 50, 50, 2.0); gesture.Tick(2.6);
    CHECK(gesture.Move(9, 70, 50, 2.7) == Event::Canceled);
    CHECK(gesture.Up(9, 70, 50, 2.8) == Event::None);
    gesture.Down(10, 50, 50, 3.0);
    CHECK(gesture.Move(10, 56, 56, 3.1) == Event::PanStarted);
}

AETHER_TEST("wheel steps: equal totals give equal steps in any packet size, sign or reversal (AS4-14, AS4-15)") {
    WheelSteps notch, tiny, mixed;
    CHECK_EQ(notch.Add(1.0, 1.0), 1);
    int total = 0;
    for (int i = 0; i < 120; ++i) total += tiny.Add(1.0 / 120.0, 1.0 + i * 0.001);
    CHECK_EQ(total, 1);                     // no rounding loss across many tiny packets
    CHECK(std::abs(tiny.Remainder()) < 1e-6);
    total = 0;
    for (int i = 0; i < 240; ++i) total += tiny.Add(-1.0 / 120.0, 2.0 + i * 0.001);
    CHECK_EQ(total, -2);
    CHECK_EQ(mixed.Add(0.6, 1.0), 0);       // a remainder carries, it is not rounded away
    CHECK_EQ(mixed.Add(0.6, 1.1), 1);
    CHECK_EQ(mixed.Add(-0.3, 1.2), 0);      // a reversal first cancels what was carried
    CHECK(std::abs(mixed.Remainder() + 0.1) < 1e-9);
    CHECK_EQ(mixed.Add(3.0, 1.3), 2);       // a large packet is several steps at once
}

AETHER_TEST("wheel steps: zero and non-finite packets do nothing; an old remainder is dropped") {
    WheelSteps steps(0.5);
    CHECK_EQ(steps.Add(0.0, 1.0), 0);
    CHECK_EQ(steps.Add(std::numeric_limits<double>::quiet_NaN(), 1.0), 0);
    CHECK_EQ(steps.Add(0.9, 1.0), 0);
    CHECK_EQ(steps.Add(0.2, 2.0), 0);       // a second later: the 0.9 no longer counts
    CHECK(std::abs(steps.Remainder() - 0.2) < 1e-9);
    steps.Reset();
    CHECK_EQ(steps.Remainder(), 0.0);
}

AETHER_TEST("press target: a release acts only on its own press, with the same button, once (AS4-11, AS4-12)") {
    PressTarget press;
    CHECK_FALSE(press.Release(0, 3).has_value());   // no press at all
    press.Press(0, 3);
    CHECK_EQ(*press.Release(0, 3), 3);
    CHECK_FALSE(press.Release(0, 3).has_value());   // only once
    press.Press(0, 3);
    CHECK_FALSE(press.Release(0, 4).has_value());   // pressed one item, released on another
    CHECK_FALSE(press.Release(0, 3).has_value());   // and that gesture is over
    press.Press(0, std::nullopt);
    CHECK_FALSE(press.Release(0, 3).has_value());   // pressed empty space
    press.Press(0, 3);
    CHECK_FALSE(press.Release(1, 3).has_value());   // another button's release
    CHECK_EQ(*press.Release(0, 3), 3);              // does not end this button's gesture
    press.Press(0, 3);
    press.Cancel();
    CHECK_FALSE(press.Release(0, 3).has_value());   // capture lost, hidden, deactivated
}
