// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "showcase/FrameCompositor.h"

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aether::showcase {

// A local, lossless frame spool for the user-started capture workflow. Callers choose a private
// temporary path; the file contains pixels and timestamps only, is never logged, and is not a
// repository artifact. Automated tests use synthetic pixels and a disposable test path.
class FrameArchive final : public ITemporalFrameSource {
public:
    FrameArchive() noexcept;
    ~FrameArchive() override;

    FrameArchive(const FrameArchive&) = delete;
    FrameArchive& operator=(const FrameArchive&) = delete;
    FrameArchive(FrameArchive&& other) noexcept;
    FrameArchive& operator=(FrameArchive&& other) noexcept;

    [[nodiscard]] static Result<FrameArchive> Create(std::wstring localFile, std::uint32_t width,
                                                      std::uint32_t height, double nominalFramesPerSecond) noexcept;
    // Reopens a completed local archive read-only for offline rendering. The timestamp index is
    // bounded to one million frames; pixel storage is allocated only on the first ReadFrameAt.
    // Close the original writer before reopening. Malformed/incomplete files fail without paths.
    [[nodiscard]] static Result<FrameArchive> Open(std::wstring localFile) noexcept;
    [[nodiscard]] Result<void> Append(double timestampSeconds, const RgbaFrameView& frame) noexcept;
    [[nodiscard]] Result<void> Finalize() noexcept;
    [[nodiscard]] Result<RgbaFrameView> ReadFrameAt(double timestampSeconds) noexcept override;
    [[nodiscard]] std::size_t FrameCount() const noexcept;
    [[nodiscard]] std::uint32_t Width() const noexcept;
    [[nodiscard]] std::uint32_t Height() const noexcept;
    [[nodiscard]] double NominalFramesPerSecond() const noexcept;
    [[nodiscard]] const std::vector<double>& CaptureTimes() const noexcept;

private:
    struct Impl;
    explicit FrameArchive(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::showcase
