// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "applications/IconPixels.h"

namespace aether::apps {
namespace {

constexpr std::size_t kBytesPerPixel = 4;  // B, G, R, A

std::uint8_t Premultiply(std::uint8_t component, std::uint8_t alpha) noexcept {
    // Exact rounded (c * a) / 255 without a division.
    const unsigned product = static_cast<unsigned>(component) * alpha + 128u;
    return static_cast<std::uint8_t>((product + (product >> 8)) >> 8);
}

}  // namespace

AlphaAnalysis AnalyzeAlpha(std::span<const std::uint8_t> pixels) noexcept {
    const std::size_t count = pixels.size() / kBytesPerPixel;
    AlphaAnalysis analysis;
    bool anyAlpha = false;
    bool anyColour = false;
    bool allOpaque = true;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* p = pixels.data() + i * kBytesPerPixel;
        const std::uint8_t alpha = p[3];
        if (alpha != 0) anyAlpha = true;
        if (alpha != 0xFF) allOpaque = false;
        if (alpha != 0 && alpha != 0xFF) ++analysis.translucentPixels;
        if (p[0] != 0 || p[1] != 0 || p[2] != 0) anyColour = true;
        if (p[0] > alpha || p[1] > alpha || p[2] > alpha) ++analysis.colourAboveAlphaPixels;
    }
    if (!anyAlpha) {
        analysis.format = anyColour ? AlphaFormat::NoAlpha : AlphaFormat::Transparent;
        analysis.colourAboveAlphaPixels = 0;  // meaningless without an alpha channel
    } else if (allOpaque) {
        analysis.format = AlphaFormat::Opaque;
    } else if (analysis.colourAboveAlphaPixels > 0) {
        analysis.format = AlphaFormat::Straight;
    } else {
        analysis.format = AlphaFormat::Ambiguous;
    }
    return analysis;
}

bool IsPremultipliedBgra(std::span<const std::uint8_t> pixels) noexcept {
    const std::size_t count = pixels.size() / kBytesPerPixel;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* p = pixels.data() + i * kBytesPerPixel;
        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3]) return false;
    }
    return true;
}

void PremultiplyBgra(std::span<std::uint8_t> pixels) noexcept {
    const std::size_t count = pixels.size() / kBytesPerPixel;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* p = pixels.data() + i * kBytesPerPixel;
        const std::uint8_t alpha = p[3];
        if (alpha == 0xFF) continue;
        p[0] = Premultiply(p[0], alpha);
        p[1] = Premultiply(p[1], alpha);
        p[2] = Premultiply(p[2], alpha);
    }
}

AlphaAnalysis NormalizeToPremultipliedBgra(std::span<std::uint8_t> pixels, AmbiguousAlpha ambiguous) noexcept {
    const AlphaAnalysis analysis = AnalyzeAlpha(pixels);
    switch (analysis.format) {
        case AlphaFormat::NoAlpha:
            for (std::size_t i = 3; i < pixels.size(); i += kBytesPerPixel) pixels[i] = 0xFF;
            break;
        case AlphaFormat::Straight:
            PremultiplyBgra(pixels);
            break;
        case AlphaFormat::Ambiguous:
            if (ambiguous == AmbiguousAlpha::TreatAsStraight) PremultiplyBgra(pixels);
            break;
        case AlphaFormat::Opaque:
        case AlphaFormat::Transparent:
            break;
    }
    return analysis;
}

}  // namespace aether::apps
