// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D62/D63: Aether's two materials drawn by one Direct3D 11 pixel shader (LiquidShader.inc), on the device
// Direct2D already uses, so that a frame can be drawn into any surface as an ordinary bitmap.
//
//   Liquid: pure water that bends the desktop picture behind it, gathers the light and casts a shadow.
//   Grain:  the frosted material, used by the arrival only; the bars keep their own Grain painting.
//
// Windows gives no sharp image of what lies behind a window (its host backdrop is already blurred). The arrival and
// the Settings previews bend the desktop picture, placed as Windows shows it; the shell's bars are transparent water
// over the live content, drawing only the water's light and shade (D65, the owner's choice).
// Rendering runs in its own Direct3D state, so it never disturbs Direct2D's.
#pragma once

#include "core/Result.h"
#include "graphics/LiquidMotion.h"
#include "graphics/Theme.h"
#include "graphics/WinRT.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace aether::graphics {

enum class MaterialStyle { Liquid, Grain };

// One frame, in the target's pixels.
struct MaterialScene {
    MaterialStyle style = MaterialStyle::Liquid;
    std::array<LiquidBody, 2> bodies{};   // pixels; radius and a2 as in LiquidBody
    float smoothness = 0.0f;              // pixels
    float lens = 1.0f;                    // 0 a ball of water, 1 a bar's edges
    float bevelDip = 14.0f;               // where the surface curves at a bar's edge (DIPs); drops curve all over
    float veil = 0.0f, blur = 0.0f;
    float logoAlpha = 0.0f, logoCx = 0.0f, logoCy = 0.0f, logoHalf = 0.0f;   // pixels
    float pxPerDip = 1.0f;
    bool background = false;   // true: the picture fills the target behind the shapes
    float shadow = 1.0f;       // 0 removes the exterior shadow
    // D65: the shell's Liquid bars are transparent water over the live content behind them: only the water's light and
    // shade are drawn (no picture). Windows gives no sharp image of what lies behind a window, and the owner chose this.
    bool transparent = false;
    // How much a bar's edge bends the picture behind it: 1 as in the prototype; 0 so that the arrival's bars end as the
    // transparent bars will look (D65).
    float edgeBend = 1.0f;
    // D66: Liquid's regular glass, as the shell's bars show it: as the drops become bars (lens towards 1) the picture
    // behind them is diffused by `glassBlurDip` and brought into `glassTokens`' band, under the glass's light.
    bool glass = false;
    LiquidGlassTokens glassTokens{};
    float glassBlurDip = 16.0f;
    D2D1_RECT_F picture{};     // where the desktop picture lies, in the target's pixels ("Fill", as Windows shows it)
};

// The arrival's frame as a scene for a target whose top-left is at `originPx` on the screen, `pxPerDip` scale.
[[nodiscard]] MaterialScene SceneFromArrival(const ArrivalFrame& frame, MaterialStyle style, D2D1_POINT_2F originPx, float pxPerDip);

class LiquidRenderer {
public:
    LiquidRenderer();
    ~LiquidRenderer();
    LiquidRenderer(const LiquidRenderer&) = delete;
    LiquidRenderer& operator=(const LiquidRenderer&) = delete;

    // Compiles the shader and creates the states on `device`, once. Resources follow the device: after a
    // device loss, Reset() and Initialize() again; the picture is kept on the CPU and uploaded again.
    [[nodiscard]] Result<void> Initialize(ID3D11Device* device);
    void Reset() noexcept;
    [[nodiscard]] bool Ready() const noexcept;

    // The desktop picture: top-down BGRA, opaque. Shared, so that callers can keep it without a copy.
    // `placementPx` is where Windows shows it, in virtual-screen pixels; surfaces read it to place it behind them.
    // The renderer keeps the CPU copy only until it is on the device: a device loss then drops the picture (HasPicture
    // turns false) and the caller reads it again when it next needs it.
    void SetPicture(std::shared_ptr<const std::vector<std::uint8_t>> bgra, UINT width, UINT height, D2D1_RECT_F placementPx = {});
    [[nodiscard]] bool HasPicture() const noexcept;
    // What only the arrival and the Settings previews use, given back once they are over: every frame's target (the
    // arrival's is the size of the screen) and, unless `keepPicture`, the desktop picture. Recreated on the next frame.
    void ReleaseTransient(bool keepPicture) noexcept;
    [[nodiscard]] SIZE PictureSize() const noexcept;
    [[nodiscard]] D2D1_RECT_F PicturePlacement() const noexcept;
    // Bumped by each SetPicture: surfaces that drew the old picture redraw.
    [[nodiscard]] std::uint64_t PictureGeneration() const noexcept;

    // Renders the scene into a texture of `sizePx` and returns it as a bitmap of `context`, which must belong to
    // the same device. Call outside the context's own drawing or after Flush(); nothing else is touched.
    [[nodiscard]] Result<winrt::com_ptr<ID2D1Bitmap1>> Render(ID2D1DeviceContext* context, const MaterialScene& scene, D2D1_SIZE_U sizePx);
    // D69: renders the scene straight into the texture `context` is drawing into, at `originPx` there (the context's
    // own offset in a composition surface), without a texture of its own nor a copy: for a frame that covers the whole
    // window (the arrival). The scene is in the frame's pixels. True when drawn; false when this target cannot take it
    // (another device, a texture it cannot write): then draw through Render(). Flushes the context first.
    [[nodiscard]] Result<bool> RenderInto(ID2D1DeviceContext* context, const MaterialScene& scene, D2D1_SIZE_U sizePx, POINT originPx);
    // The same frame's pixels (top-down BGRA, premultiplied), for a surface painted by another device.
    [[nodiscard]] Result<void> RenderToMemory(const MaterialScene& scene, D2D1_SIZE_U sizePx, std::vector<std::uint8_t>& bgra);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::graphics
