// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/FrameCompositor.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace aether::showcase {
class FrameArchive;

// Lossless capture spool: one full initial image, then only changed 32-pixel tiles. Timestamps,
// tile records and the previous image share a bounded native arena; no disk I/O occurs on Append.
// Use from one thread. FlushTo seals capture, reconstructs in the existing image buffer, and writes
// to an empty same-sized archive after the desktop session has been restored.
class CaptureBuffer final {
public:
    static constexpr std::uint64_t DefaultMaximumBytes = 512ULL * 1024 * 1024;
    static constexpr std::uint64_t MaximumBytes = 1024ULL * 1024 * 1024;

    CaptureBuffer() noexcept;
    ~CaptureBuffer();
    CaptureBuffer(const CaptureBuffer&) = delete;
    CaptureBuffer& operator=(const CaptureBuffer&) = delete;
    CaptureBuffer(CaptureBuffer&&) noexcept;
    CaptureBuffer& operator=(CaptureBuffer&&) noexcept;

    [[nodiscard]] static Result<CaptureBuffer> Create(std::uint32_t width, std::uint32_t height,
        std::uint64_t maxBytes = DefaultMaximumBytes) noexcept;
    [[nodiscard]] Result<void> Append(double timestampSeconds, const RgbaFrameView& frame) noexcept;
    [[nodiscard]] Result<void> FlushTo(FrameArchive& archive) noexcept;
    [[nodiscard]] std::size_t FrameCount() const noexcept;
    // Counts committed image/record pages and the buffer's own bookkeeping. Reserved address
    // space is not committed memory; operating-system allocator bookkeeping is outside this count.
    [[nodiscard]] std::uint64_t MemoryBytes() const noexcept;
    [[nodiscard]] std::uint64_t StoredBytes() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;

private:
    struct Impl;
    explicit CaptureBuffer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
}  // namespace aether::showcase
