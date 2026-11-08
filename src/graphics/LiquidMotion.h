// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D62/D63: the arrival after the greeting, as pure functions of time. A drop appears with the Aether mark,
// floats as weightless water, divides through a bridge that thins and lets go, and its two halves strike the
// top and the bottom of the screen, bounce and spread into the Top Bar and the Dock. Only two drops: nothing
// is left between them. The same motion serves both styles; only the material differs (LiquidRenderer).
//
// A port of stateAt() in tools/liquid-glass/welcome-bubble.html, which the owner validated, with the bars'
// real places instead of the prototype's defaults. Positions and speeds are continuous, so that any moment can
// be drawn on its own (previews, tests) and a slow frame only skips ahead.
#pragma once

#include <array>

namespace aether::graphics {

// A rounded rectangle of water, in screen DIPs. While two coincide, their smooth union is one round drop.
struct LiquidBody {
    bool present = false;
    float cx = 0.0f, cy = 0.0f;   // centre
    float hx = 0.0f, hy = 0.0f;   // half width, half height
    float radius = 0.0f;          // corner radius (at most the smaller half size)
    float a2 = 0.0f;              // the surface's second mode (long and wide), as a share of the smaller half size
};

// Where a drop lands: the real bar's panel, in screen DIPs.
struct LiquidBar {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    float radius = 0.0f;
};

struct ArrivalLayout {
    float width = 0.0f, height = 0.0f;   // the screen, DIPs
    LiquidBar topBar;
    LiquidBar dock;
};

struct ArrivalFrame {
    std::array<LiquidBody, 3> bodies{};  // 0 the upper drop, then the Top Bar; 1 the lower drop, then the Dock; 2 unused
    float smoothness = 0.0f;             // how far the union of 0 and 1 reaches, DIPs: the bridge between them
    float lens = 0.0f;                   // 0 a ball of water, 1 the bars' edges
    float logoAlpha = 0.0f;              // the mark inside the drop, until it swells to divide
    float logoCx = 0.0f, logoCy = 0.0f, logoHalf = 0.0f;
    float veil = 0.0f;                   // the welcome cover clearing: its darkness, 0..1
    float blur = 0.0f;                   //   and its blur, as a level of the picture's mipmaps
};

inline constexpr float kArrivalSeconds = 4.0f;          // the whole arrival, with the bars' contents
inline constexpr float kArrivalSettledSeconds = 3.0f;   // the bars have their shape: the shell takes over

// The arrival at `seconds` since it began. `fromCover` starts from the welcome's veiled, blurred picture.
[[nodiscard]] ArrivalFrame ArrivalAt(float seconds, const ArrivalLayout& layout, bool fromCover) noexcept;

// The damped spring the motion is made of, released at 0 towards 1 (frequency in Hz, damping ratio), and the
// same spring struck once: it starts at 0, swings and dies away. Exposed for tests.
[[nodiscard]] float SpringResponse(float seconds, float hertz, float damping) noexcept;
[[nodiscard]] float SpringRing(float seconds, float hertz, float damping) noexcept;

}  // namespace aether::graphics
