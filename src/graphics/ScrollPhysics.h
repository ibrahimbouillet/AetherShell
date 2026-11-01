// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FR-040 shared touch model, pure and deterministic (no Win32 state):
//
//   VelocityTracker  release velocity from timestamped positions (least squares, recent window)
//   ScrollPhysics    one-axis direct manipulation: the content follows the finger exactly, a release
//                    continues with the measured velocity and decelerates exponentially, and past
//                    either bound the displacement resists and returns with a critically damped
//                    spring. Logical() always stays within [0, maximum]; only Visual() leaves it.
//   TouchGesture     tap / pan / hold arbitration for one primary contact with cancellation.
//
// Units are the caller's (pixels or DIPs) and seconds on one monotonic clock (FrameScheduler::Now and
// pointer timestamps both derive from QueryPerformanceCounter). The model is
//   inertia  dv/dt = -k v              spring  y'' + 2 w y' + w^2 y = 0  (y = displacement past a bound)
// integrated exactly over bounded steps; a long gap (sleep, a stalled thread) settles at once.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace aether::graphics {

class VelocityTracker {
public:
    void Reset() noexcept { count_ = 0; next_ = 0; }
    void Add(double time, double position) noexcept;
    // Units per second over the last `window` seconds before the newest sample; 0 without two recent
    // samples or when the contact rested before release.
    [[nodiscard]] double Velocity(double window = 0.1) const noexcept;
    [[nodiscard]] std::optional<double> NewestTime() const noexcept;

private:
    struct Sample { double time = 0.0, position = 0.0; };
    std::array<Sample, 24> samples_{};
    std::size_t count_ = 0, next_ = 0;
};

struct ScrollPhysicsParameters {
    double deceleration = 3.2;          // k (1/s): a fling keeps e^-k of its speed after one second
    double minimumFling = 60.0;         // units/s; a slower release simply stops
    double maximumVelocity = 9000.0;    // units/s
    double stopVelocity = 12.0;         // inertia ends below this speed
    double springFrequency = 16.0;      // w (rad/s) of the critically damped return
    double resistance = 0.55;           // rubber-band coefficient (larger: less resistance)
    double maximumOverscroll = 160.0;   // largest visual displacement past a bound, units
    double restDistance = 0.4;
    double restVelocity = 10.0;
};

class ScrollPhysics {
public:
    enum class Phase { Idle, Tracking, Inertia, Spring };
    explicit ScrollPhysics(ScrollPhysicsParameters parameters = {}) noexcept : parameters_(parameters) {}

    void SetParameters(const ScrollPhysicsParameters& parameters) noexcept { parameters_ = parameters; }
    [[nodiscard]] const ScrollPhysicsParameters& Parameters() const noexcept { return parameters_; }
    // The scrollable range [0, maximum] and the viewport length that scales the resistance.
    void SetExtent(double maximum, double viewport) noexcept;
    // Reduced motion keeps exact direct tracking but no fling, overscroll or bounce.
    void SetReducedMotion(bool reduced) noexcept;

    // An external move (wheel, keyboard, scroll bar, UI Automation): stops any motion at `offset`.
    void JumpTo(double offset) noexcept;
    // A contact lands at finger coordinate `finger` (same axis, screen direction): motion stops where it
    // is, without a jump, and the content then follows the finger.
    void Grab(double finger, double time) noexcept;
    void Drag(double finger, double time) noexcept;
    // The contact lifted (fling with the measured velocity) or was canceled (no fling; any overscroll
    // still springs back so the view never stays displaced).
    void Release(double time) noexcept;
    void Cancel(double time) noexcept;
    // Advances inertia/spring to `time`; returns true while another frame is needed.
    bool Step(double time) noexcept;

    [[nodiscard]] Phase CurrentPhase() const noexcept { return phase_; }
    [[nodiscard]] bool Moving() const noexcept { return phase_ == Phase::Inertia || phase_ == Phase::Spring; }
    [[nodiscard]] double Visual() const noexcept { return visual_; }
    [[nodiscard]] double Logical() const noexcept;
    [[nodiscard]] double Velocity() const noexcept { return velocity_; }
    [[nodiscard]] double Maximum() const noexcept { return maximum_; }

    // Rubber band: displayed displacement for a raw drag `distance` past a bound, and its inverse.
    [[nodiscard]] double Resist(double distance) const noexcept;
    [[nodiscard]] double Unresist(double displayed) const noexcept;

private:
    [[nodiscard]] double Constrain(double raw) const noexcept;
    [[nodiscard]] double RawFor(double visual) const noexcept;
    void BeginSpring(double velocity) noexcept;
    void Settle() noexcept;

    ScrollPhysicsParameters parameters_;
    VelocityTracker tracker_;
    Phase phase_ = Phase::Idle;
    double maximum_ = 0.0;
    double viewport_ = 400.0;
    bool reduced_ = false;
    double visual_ = 0.0;
    double velocity_ = 0.0;
    double anchorFinger_ = 0.0;
    double anchorRaw_ = 0.0;
    double springBound_ = 0.0;
    double last_ = 0.0;
};

// AS4-14/AS4-15: wheel input of any packet size becomes whole steps. The signed remainder carries to
// the next packet, so equal totals travel equally far and a reversal cancels what it should; a remainder
// older than `staleSeconds` is dropped, so a small packet long after the last cannot finish an old step.
class WheelSteps {
public:
    explicit WheelSteps(double staleSeconds = 0.6) noexcept : stale_(staleSeconds) {}
    int Add(double amount, double time) noexcept;
    void Reset() noexcept { remainder_ = 0.0; }
    [[nodiscard]] double Remainder() const noexcept { return remainder_; }

private:
    double stale_;
    double remainder_ = 0.0;
    double last_ = 0.0;
};

// AS4-11/AS4-12: a release activates only the target its own press started on, with the same button.
class PressTarget {
public:
    void Press(int button, std::optional<int> target) noexcept { button_ = button; target_ = target; }
    [[nodiscard]] std::optional<int> Release(int button, std::optional<int> target) noexcept {
        if (button != button_) return std::nullopt;
        const auto pressed = target_;
        target_.reset();
        return pressed && pressed == target ? pressed : std::nullopt;
    }
    void Cancel() noexcept { target_.reset(); }

private:
    int button_ = 0;
    std::optional<int> target_;
};

struct TouchGestureParameters {
    double slop = 8.0;          // movement (caller units) that turns a press into a pan
    double holdSeconds = 0.5;   // a still contact this long becomes a hold
};
enum class TouchAxis { Vertical, Horizontal, Both };

// One primary contact. A pan, a hold that moved, a second contact, a capture loss or Cancel() never
// produces a Tap; the host invokes a command only on Tap (or on its own explicit hold action).
class TouchGesture {
public:
    enum class Phase { Idle, Pending, Panning, Holding, Canceled };
    enum class Event { None, Tap, PanStarted, PanEnded, HoldStarted, HoldEnded, Canceled };

    explicit TouchGesture(TouchGestureParameters parameters = {}, TouchAxis axis = TouchAxis::Vertical) noexcept
        : parameters_(parameters), axis_(axis) {}
    void SetParameters(const TouchGestureParameters& parameters) noexcept { parameters_ = parameters; }
    void SetAxis(TouchAxis axis) noexcept { axis_ = axis; }

    Event Down(std::uint32_t pointer, double x, double y, double time) noexcept;
    Event Move(std::uint32_t pointer, double x, double y, double time) noexcept;
    Event Up(std::uint32_t pointer, double x, double y, double time) noexcept;
    Event Tick(double time) noexcept;  // a still Pending contact becomes a hold
    Event Cancel() noexcept;           // capture lost, hidden, deactivated, locked

    [[nodiscard]] Phase CurrentPhase() const noexcept { return phase_; }
    [[nodiscard]] bool Tracking(std::uint32_t pointer) const noexcept { return phase_ != Phase::Idle && pointer_ == pointer; }
    [[nodiscard]] std::optional<std::uint32_t> Pointer() const noexcept;
    [[nodiscard]] double StartX() const noexcept { return startX_; }
    [[nodiscard]] double StartY() const noexcept { return startY_; }
    [[nodiscard]] double StartTime() const noexcept { return startTime_; }

private:
    TouchGestureParameters parameters_;
    TouchAxis axis_;
    Phase phase_ = Phase::Idle;
    std::uint32_t pointer_ = 0;
    double startX_ = 0.0, startY_ = 0.0, startTime_ = 0.0;
};

}  // namespace aether::graphics
