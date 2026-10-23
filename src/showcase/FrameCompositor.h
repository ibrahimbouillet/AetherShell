// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/CinematicSequence.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace aether::showcase {

// Straight-alpha RGBA8 source pixels. The view is borrowed and only needs to remain valid until
// the next ITemporalFrameSource::ReadFrameAt call returns.
struct RgbaFrameView final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t rowStrideBytes = 0;
    std::span<const std::uint8_t> pixels;
    // Optional immutable content identity, scoped to this frame provider. Zero means unknown and
    // forbids renderer reuse. A nonzero value must never identify two different pixel buffers.
    std::uint64_t contentId = 0;
};

class ITemporalFrameSource {
public:
    virtual ~ITemporalFrameSource() = default;

    // Return an authentic captured frame at (or nearest to) the requested media time. Implementors
    // must not expose process metadata or source paths through errors or logging.
    [[nodiscard]] virtual Result<RgbaFrameView> ReadFrameAt(double timestampSeconds) noexcept = 0;
};

struct CompositeSettings final {
    double framesPerSecond = 60.0;
    double shutterAngleDegrees = 180.0;
    std::size_t sampleCount = TemporalSampleSet::MaximumSamples;
    bool motionBlurEnabled = true;
};

struct CompositeMetrics final {
    std::size_t sourceFrameReads = 0;
    std::uint64_t elapsedMicroseconds = 0;
};

class FrameCompositor final {
public:
    // Resamples captured frames with a Catmull-Rom cubic filter and blends genuine temporal
    // samples in premultiplied-alpha space. Destination pixels are straight-alpha RGBA8.
    [[nodiscard]] static Result<CompositeMetrics> Render(
        const CinematicSequence& sequence, ITemporalFrameSource& source, double frameCenterSeconds,
        const SequenceEvents& events, std::span<std::uint8_t> destination,
        std::uint32_t destinationWidth, std::uint32_t destinationHeight,
        std::size_t destinationRowStrideBytes, const CompositeSettings& settings = {}) noexcept;
};

}  // namespace aether::showcase
