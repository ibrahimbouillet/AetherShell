// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The greeting's background (D54): the person's own desktop picture, blurred and opaque, under a light
// veil that keeps the ink legible on any picture. The picture is read on this PC only while a greeting
// is due, and is never logged, stored or sent anywhere. LoadDesktopBackdrop is the only part that touches
// the real desktop, and only the application calls it: tests hand in their own pictures.
#pragma once

#include "core/Result.h"
#include "graphics/WinRT.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace aether::welcome {

// Decoded pixels on the CPU, so that they survive a lost graphics device and cross threads freely.
struct BackdropImage {
    std::vector<std::uint8_t> pixels;  // top-down, BGRA, four bytes each
    UINT width = 0, height = 0;
    float brightness = 0.0f;           // the picture's average, 0 to 1
};

// The average brightness of top-down BGRA pixels, 0 to 1, as displayed (not linear light).
[[nodiscard]] float MeanBrightness(std::span<const std::uint8_t> pixels) noexcept;
// How dark a veil the ink needs over a picture this bright: 4.5:1 for pale ink on the average colour.
[[nodiscard]] float VeilOpacityFor(float brightness) noexcept;
// The rectangle that fills the area with the whole picture's proportions, centred (as "Fill" does).
[[nodiscard]] D2D1_RECT_F CoverRect(float imageWidth, float imageHeight, float areaWidth, float areaHeight) noexcept;

// The files to try for a monitor's picture, in order: the one Windows names, then `windowsCopy`, the copy
// of the picture Windows itself shows. None when Windows shows a plain colour (both names empty). Windows
// names no file for a monitor whose picture was moved or deleted, yet still shows it from its copy (D61):
// `desktopPicture`, the desktop-wide setting, still holds the old name then.
[[nodiscard]] std::vector<std::filesystem::path> BackdropCandidates(std::wstring_view monitorPicture, std::wstring_view desktopPicture,
                                                                    const std::filesystem::path& windowsCopy);
// Decodes a picture, reduced until it just covers `coverWidth` x `coverHeight` pixels; never enlarged.
[[nodiscard]] Result<BackdropImage> DecodeBackdrop(const std::filesystem::path& file, UINT coverWidth, UINT coverHeight);
// The desktop picture of the monitor at `monitorPx`; nullopt when it is a plain colour or cannot be read.
// Runs on a worker thread with COM initialized. Half the monitor's resolution is plenty under the blur.
[[nodiscard]] std::optional<BackdropImage> LoadDesktopBackdrop(const RECT& monitorPx);

// The blurred picture, once, on the device: it is then simply drawn each frame.
[[nodiscard]] winrt::com_ptr<ID2D1Bitmap1> BlurBackdrop(ID2D1Device* device, const BackdropImage& image);
// Fills the area: the blurred picture if there is one, otherwise `solid`, then the veil.
void PaintBackdrop(ID2D1DeviceContext* context, ID2D1Bitmap* blurred, const D2D1_COLOR_F& solid, float brightness, float width, float height);

}  // namespace aether::welcome
