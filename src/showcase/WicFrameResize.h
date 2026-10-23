// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/FrameCompositor.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace aether::showcase {
// High-quality native resampling with explicit RGBA output conversion. WIC scalers can select
// another internal pixel format; never interpret CopyPixels bytes without that conversion.
// Requires COM on the calling thread. Buffers must include complete stride*height storage and
// must not overlap. The caller retains ownership; no files or desktop state are accessed.
[[nodiscard]] Result<void> ResizeRgbaFrame(const RgbaFrameView& source,
    std::span<std::uint8_t> destination, std::uint32_t outputWidth, std::uint32_t outputHeight,
    std::size_t outputRowStrideBytes) noexcept;
}  // namespace aether::showcase
