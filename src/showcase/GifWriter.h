// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/FrameCompositor.h"

#include <cstdint>
#include <memory>
#include <string>

namespace aether::showcase {

struct GifWriterOptions final {
    std::wstring outputFile;
    std::uint32_t width = 960;
    std::uint32_t height = 540;
    std::uint32_t frameDelayCentiseconds = 5;  // 20 FPS; GIF delays have 10-ms precision.
};

// Native WIC GIF89a writer with an infinite loop. The first opaque RGBA frame supplies a single
// optimized palette, reused for every frame to avoid changing color assignments over time.
// Requires an initialized COM apartment; use and destroy on that apartment's thread. Paths never
// appear in errors or logs. Finalize publishes only a completed file and never replaces a file.
class GifWriter final {
public:
    GifWriter() noexcept;
    ~GifWriter();
    GifWriter(const GifWriter&) = delete;
    GifWriter& operator=(const GifWriter&) = delete;
    GifWriter(GifWriter&&) noexcept;
    GifWriter& operator=(GifWriter&&) noexcept;

    [[nodiscard]] static Result<GifWriter> Create(GifWriterOptions options) noexcept;
    [[nodiscard]] Result<void> AppendFrame(const RgbaFrameView& frame) noexcept;
    [[nodiscard]] Result<void> Finalize() noexcept;
    [[nodiscard]] std::uint64_t FramesWritten() const noexcept;

private:
    struct Impl;
    explicit GifWriter(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
