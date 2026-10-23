// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "showcase/FrameCompositor.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace aether::showcase {

struct Mp4WriterOptions final {
    std::wstring outputFile;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t framesPerSecond = 60;
    std::uint32_t averageBitrate = 18'000'000;
    bool includeAudio = false;
    std::uint32_t audioSampleRate = 48'000;
    std::uint32_t audioChannels = 2;
    std::uint32_t audioBitrate = 192'000;
};

// Windows Media Foundation H.264/MP4 writer. File paths are runtime data and are never included in
// returned error text or logs. Call Finalize explicitly to receive the encoder's completion result.
class Mp4Writer final {
public:
    Mp4Writer() noexcept;
    ~Mp4Writer();

    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;
    Mp4Writer(Mp4Writer&& other) noexcept;
    Mp4Writer& operator=(Mp4Writer&& other) noexcept;

    [[nodiscard]] static Result<Mp4Writer> Create(Mp4WriterOptions options) noexcept;
    [[nodiscard]] Result<void> AppendFrame(const RgbaFrameView& frame) noexcept;
    [[nodiscard]] Result<void> AppendAudio(std::span<const std::int16_t> interleavedPcm) noexcept;
    [[nodiscard]] Result<void> Finalize() noexcept;
    [[nodiscard]] std::uint64_t FramesWritten() const noexcept;

private:
    struct Impl;
    explicit Mp4Writer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
