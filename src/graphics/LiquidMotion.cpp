// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/LiquidMotion.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aether::graphics {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;
// The prototype's constants (tools/liquid-glass/welcome-bubble.html), unchanged.
constexpr float kSmooth = 1.25f;                 // reach of the union, as a share of a lobe's radius
constexpr float kSwell = 1.0f + kSmooth / 4.0f;  // two coincident lobes of radius r make one drop of r * kSwell
constexpr float kTravelStart = 1.42f, kTravelUp = 0.84f, kTravelDown = 0.9f, kStretch = 0.18f;

float Clamp(float x, float a, float b) noexcept { return std::min(b, std::max(a, x)); }
float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
float Smooth(float a, float b, float x) noexcept { const float t = Clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

struct Drop { float cx, cy, radius, a2; };
struct Travel { float s, v; };

// It forms from a point, settles with one soft overshoot, then floats: weightless water swings slowly between
// long and wide, drifting a little, and grows calm before it divides.
Drop DropAt(float t, float width, float height, float r0, float rBig) noexcept {
    const float radius = r0 * SpringResponse(t, 1.2f, 0.74f) + (rBig - r0) * Smooth(1.05f, 1.5f, t);
    const float formed = Smooth(0.2f, 0.75f, t), calm = 1.0f - Smooth(1.15f, 1.5f, t);
    const float a2 = 0.034f * std::sin(2.0f * kPi * 1.1f * (t - 0.2f)) * formed * calm;
    const float cy = height / 2.0f - r0 * 0.03f * std::sin(2.0f * kPi * 0.3f * t) * formed * calm;
    return {width / 2.0f, cy, radius, a2};
}

// How far a lobe has moved from the centre: slowly while the bridge holds, then faster and faster.
Travel TravelAt(float t, float distance, float duration) noexcept {
    const float u = Clamp((t - kTravelStart) / duration, 0.0f, 1.0f);
    return {distance * u * u * u, u * u};
}

// The moment the bridge snaps: the lobes' gap reaches half the union's reach.
float BreakTime(float up, float down, float lobe) noexcept {
    for (float t = kTravelStart; t < kTravelStart + kTravelDown; t += 0.002f)
        if (TravelAt(t, up, kTravelUp).s + TravelAt(t, down, kTravelDown).s >= 2.0f * lobe * kSwell) return t;
    return kTravelStart + 0.5f;
}

// A lobe after it strikes its edge: it flattens against it, bounces off a little and spreads into its bar,
// carrying on from the moment of impact without a jump.
LiquidBody Settle(float t, float impact, float lobe, const LiquidBar& bar, bool upper, float startCx, const ArrivalLayout& layout) noexcept {
    const float tau = t - impact;
    const float hyStart = lobe * (1.0f + kStretch), hxStart = lobe / (1.0f + kStretch);
    const float barHx = (bar.right - bar.left) / 2.0f, barHy = (bar.bottom - bar.top) / 2.0f, barCx = (bar.left + bar.right) / 2.0f;
    const float edge = upper ? bar.top : layout.height - bar.bottom;
    const float squash = SpringRing(tau, 2.8f, 0.38f);
    const float hy = std::max(Lerp(hyStart, barHy, SpringResponse(tau, 2.5f, 0.72f)) * (1.0f - 0.3f * squash), barHy * 0.7f);
    // The Top Bar spreads without overshooting the screen; the Dock may swing a little past its width.
    const float spread = upper ? SpringResponse(tau - 0.02f, 1.3f, 0.86f) : SpringResponse(tau - 0.01f, 1.85f, 0.62f);
    const float cx = Lerp(startCx, barCx, Clamp(spread, 0.0f, 1.0f));
    const float room = std::max(1.0f, std::min(cx, layout.width - cx) - 2.0f);
    const float hx = std::min(Lerp(hxStart, barHx, spread) + lobe * 0.3f * std::max(0.0f, squash), room);
    const float away = edge * SpringResponse(tau, 2.2f, 0.7f) + (upper ? 10.0f : 13.0f) * SpringRing(tau, 1.8f, 0.4f);   // rebound
    LiquidBody body;
    body.present = true;
    body.cx = cx;
    body.cy = upper ? away + hy : layout.height - away - hy;
    body.hx = hx;
    body.hy = hy;
    body.radius = std::min({Lerp(std::min(hx, hy), bar.radius, Smooth(0.0f, 0.5f, tau)), hx, hy});
    return body;
}
}  // namespace

float SpringResponse(float t, float hertz, float damping) noexcept {
    if (t <= 0.0f) return 0.0f;
    const float w = 2.0f * kPi * hertz;
    if (damping >= 1.0f) return 1.0f - std::exp(-w * t) * (1.0f + w * t);
    const float wd = w * std::sqrt(1.0f - damping * damping);
    return 1.0f - std::exp(-damping * w * t) * (std::cos(wd * t) + (damping * w / wd) * std::sin(wd * t));
}

float SpringRing(float t, float hertz, float damping) noexcept {
    if (t <= 0.0f) return 0.0f;
    const float w = 2.0f * kPi * hertz;
    return std::exp(-damping * w * t) * std::sin(w * std::sqrt(1.0f - damping * damping) * t);
}

ArrivalFrame ArrivalAt(float t, const ArrivalLayout& layout, bool fromCover) noexcept {
    ArrivalFrame frame;
    const float width = std::max(layout.width, 1.0f), height = std::max(layout.height, 1.0f);
    const float r0 = Clamp(std::min(width, height) * 0.085f, 54.0f, 118.0f), rBig = r0 * 1.14f;
    const Drop drop = DropAt(t, width, height, r0, rBig);
    const float lobe = drop.radius / kSwell;
    frame.smoothness = kSmooth * lobe;
    const float settledLobe = rBig / kSwell;
    // Both journeys run from the screen's centre until the drawn-out lobe touches the screen's edge; the
    // bars' margins are then reached by the rebound, as in the prototype.
    const float upDistance = height / 2.0f - settledLobe * (1.0f + kStretch);
    const float downDistance = upDistance;
    const Travel upper = TravelAt(t, upDistance, kTravelUp), lower = TravelAt(t, downDistance, kTravelDown);
    const float broken = BreakTime(upDistance, downDistance, settledLobe);
    const float impactUp = kTravelStart + kTravelUp, impactDown = kTravelStart + kTravelDown;
    const auto lobeAt = [&](bool isUpper, const Travel& moved, float impact, const LiquidBar& bar) {
        if (t >= impact) return Settle(t, impact, lobe, bar, isUpper, drop.cx, layout);
        const float stretch = 1.0f + kStretch * moved.v * Smooth(broken, broken + 0.14f, t);   // round while joined
        const float recoil = 0.06f * SpringRing(t - broken, 2.6f, 0.3f);                      // the bridge returns into it
        LiquidBody body;
        body.present = t > 0.0f;
        body.cx = drop.cx;
        body.cy = drop.cy + (isUpper ? -moved.s : moved.s);
        body.hx = std::max(lobe / stretch, 0.01f);
        body.hy = std::max(lobe * stretch, 0.01f);
        body.radius = lobe * stretch;
        body.a2 = drop.a2 + recoil;
        return body;
    };
    frame.bodies[0] = lobeAt(true, upper, impactUp, layout.topBar);
    frame.bodies[1] = lobeAt(false, lower, impactDown, layout.dock);
    frame.lens = Smooth(impactUp - 0.05f, impactDown + 0.55f, t);
    frame.logoAlpha = Smooth(0.15f, 0.45f, t) * (1.0f - Smooth(1.12f, 1.38f, t));
    frame.logoCx = drop.cx;
    frame.logoCy = drop.cy;
    frame.logoHalf = drop.radius * 0.55f;
    if (fromCover) {
        const float cover = 1.0f - Smooth(0.0f, 0.7f, t);
        frame.veil = 0.5f * cover;
        frame.blur = 4.2f * cover;
    }
    return frame;
}

}  // namespace aether::graphics
