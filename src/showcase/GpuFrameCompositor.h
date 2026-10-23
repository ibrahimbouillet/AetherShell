// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "showcase/FrameCompositor.h"

#include <cstdint>
#include <memory>
#include <span>

namespace aether::showcase {

struct GpuCompositeMetrics final {
    std::size_t sourceFrameReads = 0;
    std::size_t sourceTextureUploads = 0;
    std::uint64_t elapsedMicroseconds = 0;
    bool hardwareAccelerated = false;
};

// Direct3D reference-to-production compositor. Samples real temporal source frames, evaluates the
// authored camera at each shutter timestamp, averages in linear light, and reads the final SDR
// sRGB image back for the Media Foundation writer. Device fallback uses WARP and is reported.
class GpuFrameCompositor final {
public:
    GpuFrameCompositor() noexcept;
    ~GpuFrameCompositor();

    GpuFrameCompositor(const GpuFrameCompositor&) = delete;
    GpuFrameCompositor& operator=(const GpuFrameCompositor&) = delete;
    GpuFrameCompositor(GpuFrameCompositor&& other) noexcept;
    GpuFrameCompositor& operator=(GpuFrameCompositor&& other) noexcept;

    [[nodiscard]] static Result<GpuFrameCompositor> Create() noexcept;
    [[nodiscard]] Result<GpuCompositeMetrics> Render(
        const CinematicSequence& sequence, ITemporalFrameSource& source, double frameCenterSeconds,
        const SequenceEvents& events, std::span<std::uint8_t> destination,
        std::uint32_t destinationWidth, std::uint32_t destinationHeight,
        std::size_t destinationRowStrideBytes, const CompositeSettings& settings = {}) noexcept;

    [[nodiscard]] bool HardwareAccelerated() const noexcept;

private:
    struct Impl;
    explicit GpuFrameCompositor(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
