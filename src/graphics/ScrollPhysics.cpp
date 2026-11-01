// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/ScrollPhysics.h"

#include <algorithm>
#include <cmath>

namespace aether::graphics {
namespace {
constexpr double kStep = 1.0 / 240.0;   // integration substep
constexpr double kMaximumGap = 0.25;    // a longer frame gap (sleep, stall) settles instead of jumping
constexpr double kRestedRelease = 0.08; // a contact still this long before lifting has no fling
bool Finite(double value) noexcept { return std::isfinite(value); }
}  // namespace

void VelocityTracker::Add(double time, double position) noexcept {
    if (!Finite(time) || !Finite(position)) return;
    // Out-of-order timestamps (a coalesced batch) restart the estimate rather than reverse it.
    if (const auto newest = NewestTime(); newest && time < *newest) Reset();
    samples_[next_] = Sample{time, position};
    next_ = (next_ + 1) % samples_.size();
    count_ = std::min(count_ + 1, samples_.size());
}

std::optional<double> VelocityTracker::NewestTime() const noexcept {
    if (!count_) return std::nullopt;
    return samples_[(next_ + samples_.size() - 1) % samples_.size()].time;
}

double VelocityTracker::Velocity(double window) const noexcept {
    if (count_ < 2) return 0.0;
    const double newest = *NewestTime();
    double sumT = 0.0, sumX = 0.0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        const auto& sample = samples_[(next_ + samples_.size() - 1 - i) % samples_.size()];
        if (newest - sample.time > window) break;
        sumT += sample.time; sumX += sample.position; ++used;
    }
    if (used < 2) return 0.0;
    const double meanT = sumT / static_cast<double>(used), meanX = sumX / static_cast<double>(used);
    double covariance = 0.0, variance = 0.0;
    for (std::size_t i = 0; i < used; ++i) {
        const auto& sample = samples_[(next_ + samples_.size() - 1 - i) % samples_.size()];
        covariance += (sample.time - meanT) * (sample.position - meanX);
        variance += (sample.time - meanT) * (sample.time - meanT);
    }
    if (variance <= 1e-12) return 0.0;
    const double velocity = covariance / variance;
    return Finite(velocity) ? velocity : 0.0;
}

void ScrollPhysics::SetExtent(double maximum, double viewport) noexcept {
    maximum_ = Finite(maximum) ? std::max(0.0, maximum) : 0.0;
    viewport_ = Finite(viewport) ? std::max(1.0, viewport) : 1.0;
    if (phase_ == Phase::Idle) visual_ = std::clamp(visual_, 0.0, maximum_);
    else if (phase_ == Phase::Inertia && (visual_ < 0.0 || visual_ > maximum_)) BeginSpring(velocity_);
}

void ScrollPhysics::SetReducedMotion(bool reduced) noexcept {
    reduced_ = reduced;
    if (reduced_ && phase_ != Phase::Tracking) Settle();
    else if (reduced_) visual_ = std::clamp(visual_, 0.0, maximum_);
}

void ScrollPhysics::JumpTo(double offset) noexcept {
    phase_ = Phase::Idle;
    velocity_ = 0.0;
    visual_ = Finite(offset) ? std::clamp(offset, 0.0, maximum_) : Logical();
}

double ScrollPhysics::Logical() const noexcept { return std::clamp(visual_, 0.0, maximum_); }

double ScrollPhysics::Resist(double distance) const noexcept {
    if (!(distance > 0.0)) return 0.0;
    const double c = std::max(0.01, parameters_.resistance);
    const double displayed = (1.0 - 1.0 / (distance * c / viewport_ + 1.0)) * viewport_;
    return std::min(displayed, std::max(0.0, parameters_.maximumOverscroll));
}

double ScrollPhysics::Unresist(double displayed) const noexcept {
    if (!(displayed > 0.0)) return 0.0;
    const double c = std::max(0.01, parameters_.resistance);
    const double bounded = std::min({displayed, std::max(0.0, parameters_.maximumOverscroll), viewport_ * 0.999});
    return (viewport_ / c) * (1.0 / (1.0 - bounded / viewport_) - 1.0);
}

double ScrollPhysics::Constrain(double raw) const noexcept {
    if (reduced_) return std::clamp(raw, 0.0, maximum_);
    if (raw < 0.0) return -Resist(-raw);
    if (raw > maximum_) return maximum_ + Resist(raw - maximum_);
    return raw;
}

double ScrollPhysics::RawFor(double visual) const noexcept {
    if (visual < 0.0) return -Unresist(-visual);
    if (visual > maximum_) return maximum_ + Unresist(visual - maximum_);
    return visual;
}

void ScrollPhysics::Grab(double finger, double time) noexcept {
    if (!Finite(finger) || !Finite(time)) return;
    // Stops a fling or spring exactly where it is; the content then moves only with the finger.
    phase_ = Phase::Tracking;
    velocity_ = 0.0;
    anchorFinger_ = finger;
    anchorRaw_ = RawFor(visual_);
    tracker_.Reset();
    tracker_.Add(time, anchorRaw_);
    last_ = time;
}

void ScrollPhysics::Drag(double finger, double time) noexcept {
    if (!Finite(finger) || !Finite(time)) return;
    if (phase_ != Phase::Tracking) { Grab(finger, time); return; }
    // Screen direction: the finger moving up (decreasing) advances the offset by the same amount.
    const double raw = anchorRaw_ + (anchorFinger_ - finger);
    visual_ = Constrain(raw);
    tracker_.Add(time, raw);
    last_ = time;
}

void ScrollPhysics::Release(double time) noexcept {
    if (phase_ != Phase::Tracking) return;
    double velocity = tracker_.Velocity();
    if (const auto newest = tracker_.NewestTime(); !newest || !Finite(time) || time - *newest > kRestedRelease) velocity = 0.0;
    const double limit = std::max(0.0, parameters_.maximumVelocity);
    velocity = std::clamp(velocity, -limit, limit);
    last_ = Finite(time) ? time : last_;
    if (reduced_) { Settle(); return; }
    if (visual_ < 0.0 || visual_ > maximum_) {
        // Past a bound only an inward release keeps its speed; the spring returns either way.
        const bool outward = visual_ < 0.0 ? velocity < 0.0 : velocity > 0.0;
        BeginSpring(outward ? 0.0 : velocity);
        return;
    }
    if (std::abs(velocity) >= parameters_.minimumFling) {
        phase_ = Phase::Inertia;
        velocity_ = velocity;
        return;
    }
    Settle();
}

void ScrollPhysics::Cancel(double time) noexcept {
    if (phase_ != Phase::Tracking) return;
    last_ = Finite(time) ? time : last_;
    if (!reduced_ && (visual_ < 0.0 || visual_ > maximum_)) BeginSpring(0.0);
    else Settle();
}

void ScrollPhysics::BeginSpring(double velocity) noexcept {
    phase_ = Phase::Spring;
    velocity_ = Finite(velocity) ? velocity : 0.0;
    springBound_ = visual_ < 0.0 ? 0.0 : maximum_;
}

void ScrollPhysics::Settle() noexcept {
    phase_ = Phase::Idle;
    velocity_ = 0.0;
    visual_ = Logical();
}

bool ScrollPhysics::Step(double time) noexcept {
    if (phase_ != Phase::Inertia && phase_ != Phase::Spring) return false;
    if (!Finite(time)) { Settle(); return false; }
    double remaining = time - last_;
    last_ = time;  // input timestamps ahead of the frame clock must not stall the motion until it catches up
    if (remaining <= 0.0) return true;
    if (remaining > kMaximumGap) {
        // Never integrate across a suspended or stalled interval: land on a valid rest state.
        Settle();
        return false;
    }
    const double cap = std::max(0.0, parameters_.maximumOverscroll);
    while (remaining > 0.0 && (phase_ == Phase::Inertia || phase_ == Phase::Spring)) {
        const double h = std::min(kStep, remaining);
        remaining -= h;
        if (phase_ == Phase::Inertia) {
            const double k = std::max(0.01, parameters_.deceleration);
            const double decay = std::exp(-k * h);
            visual_ += velocity_ * (1.0 - decay) / k;
            velocity_ *= decay;
            if (visual_ < 0.0 || visual_ > maximum_) {
                if (reduced_) { Settle(); break; }
                BeginSpring(velocity_);  // the content runs into the edge and is caught by the spring
                continue;
            }
            if (std::abs(velocity_) < parameters_.stopVelocity) { Settle(); break; }
            continue;
        }
        // Critically damped spring towards the bound: y = (A + B t) e^(-w t), A = y0, B = v0 + w y0.
        const double w = std::max(0.1, parameters_.springFrequency);
        const double a = visual_ - springBound_;
        const double b = velocity_ + w * a;
        const double e = std::exp(-w * h);
        double y = (a + b * h) * e;
        double v = (b - w * (a + b * h)) * e;
        if (std::abs(y) > cap) {  // the displacement never exceeds its visual bound
            y = std::copysign(cap, y);
            if ((y > 0.0) == (v > 0.0)) v = 0.0;
        }
        const bool crossed = a != 0.0 && ((a > 0.0) != (y > 0.0)) && y != 0.0;
        visual_ = springBound_ + y;
        velocity_ = v;
        if (crossed || (std::abs(y) < parameters_.restDistance && std::abs(v) < parameters_.restVelocity)) {
            visual_ = springBound_;
            velocity_ = 0.0;
            phase_ = Phase::Idle;
        }
    }
    return phase_ == Phase::Inertia || phase_ == Phase::Spring;
}

int WheelSteps::Add(double amount, double time) noexcept {
    if (!Finite(amount) || !Finite(time) || amount == 0.0) return 0;
    if (time - last_ > stale_) remainder_ = 0.0;
    last_ = time;
    remainder_ += amount;
    // Many tiny packets must add up to the whole step their total says, despite rounding.
    const double whole = std::trunc(remainder_ + std::copysign(1e-9, remainder_));
    remainder_ -= whole;
    return static_cast<int>(std::clamp(whole, -1000000.0, 1000000.0));
}

TouchGesture::Event TouchGesture::Down(std::uint32_t pointer, double x, double y, double time) noexcept {
    if (phase_ == Phase::Idle) {
        phase_ = Phase::Pending;
        pointer_ = pointer;
        startX_ = x; startY_ = y; startTime_ = time;
        return Event::None;
    }
    if (pointer == pointer_) return Event::None;
    // A second finger is never part of a tap or a hold of the first one.
    if (phase_ == Phase::Pending || phase_ == Phase::Holding) { phase_ = Phase::Canceled; return Event::Canceled; }
    return Event::None;
}

TouchGesture::Event TouchGesture::Move(std::uint32_t pointer, double x, double y, double) noexcept {
    if (phase_ == Phase::Idle || pointer != pointer_) return Event::None;
    const double dx = std::abs(x - startX_), dy = std::abs(y - startY_);
    const double slop = std::max(0.0, parameters_.slop);
    if (phase_ == Phase::Pending) {
        const double along = axis_ == TouchAxis::Vertical ? dy : axis_ == TouchAxis::Horizontal ? dx : std::hypot(dx, dy);
        const double across = axis_ == TouchAxis::Vertical ? dx : axis_ == TouchAxis::Horizontal ? dy : 0.0;
        if (along >= slop && along >= across) { phase_ = Phase::Panning; return Event::PanStarted; }
        if (across >= slop) { phase_ = Phase::Canceled; return Event::Canceled; }
        return Event::None;
    }
    if (phase_ == Phase::Holding && std::hypot(dx, dy) >= slop) { phase_ = Phase::Canceled; return Event::Canceled; }
    return Event::None;
}

TouchGesture::Event TouchGesture::Up(std::uint32_t pointer, double, double, double) noexcept {
    if (phase_ == Phase::Idle || pointer != pointer_) return Event::None;
    const auto phase = phase_;
    phase_ = Phase::Idle;
    switch (phase) {
        case Phase::Pending: return Event::Tap;
        case Phase::Panning: return Event::PanEnded;
        case Phase::Holding: return Event::HoldEnded;
        default: return Event::None;
    }
}

TouchGesture::Event TouchGesture::Tick(double time) noexcept {
    if (phase_ == Phase::Pending && time - startTime_ >= parameters_.holdSeconds) { phase_ = Phase::Holding; return Event::HoldStarted; }
    return Event::None;
}

TouchGesture::Event TouchGesture::Cancel() noexcept {
    const auto phase = phase_;
    phase_ = Phase::Idle;
    return phase == Phase::Idle || phase == Phase::Canceled ? Event::None : Event::Canceled;
}

std::optional<std::uint32_t> TouchGesture::Pointer() const noexcept {
    if (phase_ == Phase::Idle) return std::nullopt;
    return pointer_;
}

}  // namespace aether::graphics
