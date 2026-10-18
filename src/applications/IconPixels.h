// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pixel-format normalization for icons read back from GDI bitmaps. The Dock composes premultiplied
// BGRA (Direct2D's native format), while 32-bpp GDI bitmaps carry one of:
//   * no alpha channel at all (every alpha byte zero),
//   * straight (non-premultiplied) alpha,
//   * premultiplied alpha.
// Straight data is recognizable whenever a colour component exceeds its pixel's alpha, which
// premultiplied data never does. The converse is not decidable: a straight image whose translucent
// pixels all happen to be dark looks exactly like premultiplied data, so the caller chooses how such
// "ambiguous" images are treated (see ShellIconProvider.h for the measured shell behaviour).
// Pure functions over byte buffers so they can be unit tested.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace aether::apps {

enum class AlphaFormat {
    NoAlpha,      // every alpha byte zero but colour present: an alpha-less bitmap
    Transparent,  // every byte zero: nothing to draw
    Opaque,       // every alpha byte 255: both representations coincide
    Ambiguous,    // translucent pixels, every colour <= alpha: premultiplied, or straight and dark
    Straight,     // some colour component exceeds its alpha: certainly straight alpha
};

struct AlphaAnalysis {
    AlphaFormat format = AlphaFormat::Transparent;
    std::size_t translucentPixels = 0;       // 0 < alpha < 255
    std::size_t colourAboveAlphaPixels = 0;  // pixels proving straight alpha
};

enum class AmbiguousAlpha {
    TreatAsStraight,       // premultiply ambiguous images too
    TreatAsPremultiplied,  // leave ambiguous images untouched
};

// Classifies 32-bpp BGRA pixels. `pixels.size()` should be a multiple of 4; a trailing partial pixel
// is ignored.
[[nodiscard]] AlphaAnalysis AnalyzeAlpha(std::span<const std::uint8_t> pixels) noexcept;

// True when every pixel satisfies B, G, R <= A (the invariant of premultiplied BGRA).
[[nodiscard]] bool IsPremultipliedBgra(std::span<const std::uint8_t> pixels) noexcept;

// Multiplies B, G and R by A / 255 (rounded) in place.
void PremultiplyBgra(std::span<std::uint8_t> pixels) noexcept;

// Converts in place to premultiplied BGRA and returns the analysis of the input:
//   NoAlpha -> alpha set to 255; Straight -> premultiplied; Ambiguous -> per `ambiguous`;
//   Opaque and Transparent -> unchanged (callers treat Transparent as "no icon").
[[nodiscard]] AlphaAnalysis NormalizeToPremultipliedBgra(std::span<std::uint8_t> pixels,
                                                         AmbiguousAlpha ambiguous) noexcept;

}  // namespace aether::apps
